// SPDX-License-Identifier: GPL-3.0-or-later
// The formations and GoalTrackHuman (docs/research/ai.md#formations): slots kept in 1/16 m and turned by the leader's
// heading, the nearest follower to each slot, the re-plan, and a tracker without a slot that only turns.
#include "ai/formations.h"

#include <cmath>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/move_action.h"
#include "ai/track_human_goal.h"
#include "ai/turn_action.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::Formation;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// Whether `a` and `b` are within 1/32 m in plan.
bool near(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y) <= 1.0F / 32.0F; }

} // namespace

TEST_CASE("a follow slot is kept in sixteenths of a metre and turned by the leader's heading", "[ai][formations]") {
    AiScene scene;
    Brain& leader = scene.player();
    Formation& formation = *scene.brains.formations().of(leader, true);
    formation.setSlot(0, 1.03F, -2.0F, 0, 0);
    CHECK(formation.slot(0, 0).offset[0] == 16);
    CHECK(formation.slot(0, 0).offset[1] == -32);
    formation.setSlot(9, 1.0F, 1.0F, 0, 0); // out of range: nothing
    formation.setSlot(0, 1.0F, 1.0F, 4, 0);

    // Facing +y, x is to his right (+x) and y ahead (+y).
    Brain& follower = scene.add({44.0F, 36.0F, 0.0F}, 0.0F);
    REQUIRE(formation.join(follower));
    CHECK(follower.following() == &formation);
    formation.plan(0);
    CHECK(near(formation.slotPoint(follower).value_or(Vec3{}), {41.0F, 38.0F, 0.0F}));

    // Facing -x (90 degrees), his right is +y.
    leader.human().spawn(scene.mesh.get(), {40.0F, 40.0F, 0.0F}, 90.0F);
    formation.plan(0);
    CHECK(near(formation.slotPoint(follower).value_or(Vec3{}), {42.0F, 41.0F, 0.0F}));
}

TEST_CASE("each slot takes its nearest follower; the rest queue behind, and the plan repeats every 2 s",
          "[ai][formations]") {
    AiScene scene;
    Brain& leader = scene.player();
    Formation& formation = *scene.brains.formations().of(leader, true);
    formation.setSlotCount(2, -1, 0);
    CHECK(formation.allowed() == 2);
    formation.setSlot(0, 2.0F, 0.0F, 0, 0);  // his right
    formation.setSlot(1, -2.0F, 0.0F, 0, 0); // his left
    Brain& left = scene.add({36.0F, 40.0F, 0.0F}, 0.0F);
    Brain& right = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& extra = scene.add({40.0F, 34.0F, 0.0F}, 0.0F);
    REQUIRE(formation.join(left));
    REQUIRE(formation.join(right));
    REQUIRE(formation.join(extra));
    formation.plan(1000);
    CHECK(formation.follower(right)->slot == 0);
    CHECK(formation.follower(left)->slot == 1);
    CHECK(formation.follower(extra)->slot == -1);
    CHECK(formation.follower(extra)->behind != nullptr);
    CHECK(formation.nextPlanMs() == 1000 + coney::ai::kPlanOtherMs);

    // Not yet due: no plan; the leader a metre from the plan point: a plan at once.
    formation.update(1500);
    CHECK(formation.nextPlanMs() == 1000 + coney::ai::kPlanOtherMs);
    leader.human().spawn(scene.mesh.get(), {40.0F, 41.5F, 0.0F}, 0.0F);
    formation.update(1600);
    CHECK(formation.nextPlanMs() == 1600 + coney::ai::kPlanOtherMs);

    // Leaving clears the follower's formation.
    formation.leave(extra);
    CHECK(extra.following() == nullptr);
    CHECK(formation.follower(extra) == nullptr);
}

TEST_CASE("GoalTrackHuman walks to its slot, and without a slot only turns to the target", "[ai][formations]") {
    AiScene scene;
    Brain& leader = scene.player();
    Formation& formation = *scene.brains.formations().of(leader, true);

    // No slots: it turns to face the leader and never moves.
    formation.setSlotCount(0, -1, 0);
    Brain& tracker = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    REQUIRE(coney::ai::goalTrackHuman(tracker, scene.services, scene.brains.formations(), leader.handle(), 1.0F));
    scene.run(2);
    CHECK(tracker.following() == &formation);
    REQUIRE(tracker.frontAction() != nullptr);
    CHECK(dynamic_cast<coney::ai::TurnAction*>(tracker.frontAction()) != nullptr);
    scene.run(60);
    CHECK(std::hypot(tracker.human().position().x - 45.0F, tracker.human().position().y - 40.0F) < 0.05F);

    // A slot 3 m behind the leader, beyond the distance: it walks there.
    formation.setSlotCount(1, -1, scene.brains.nowMs());
    formation.setSlot(0, 0.0F, -3.0F, 0, scene.brains.nowMs());
    tracker.clearActions();
    scene.run(1);
    REQUIRE(tracker.frontAction() != nullptr);
    CHECK(dynamic_cast<coney::ai::MoveAction*>(tracker.frontAction()) != nullptr);

    // The goal ends: the tracker leaves the formation.
    tracker.flush();
    CHECK(tracker.following() == nullptr);
}
