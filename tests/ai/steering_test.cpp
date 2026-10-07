// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/steering.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "human/human.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The steering's blocker test (docs/research/ai.md#neighbour-sectors, the blocker test): the step clamp, the ray
// against a human's disc, the predicted steps and the first human met. Synthetic humans on a floor.

using Catch::Approx;
using coney::ai::Brain;
using coney::ai::BrainType;
using coney::anim::Vec3;
using coney::human::Human;
namespace ai = coney::ai;

namespace {

// Humans on an 80 m floor, each with a brain.
struct Scene {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    std::vector<std::unique_ptr<Human>> humans;
    ai::Brains brains;

    // A human with its feet at (x, y) facing `headingDegrees` (0 is +y), with a brain of `type`.
    Brain& add(float x, float y, float headingDegrees, BrainType type = BrainType::Gang) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        made.setFighterProfile(coney::human::FighterProfile{
            .player = type == BrainType::Player, .powerClass = ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), Vec3{x, y, 0.0F}, headingDegrees);
        return brains.add(made, type, ai::FightSettings{}, static_cast<std::uint32_t>(humans.size()));
    }
};

} // namespace

TEST_CASE("the clamped step is the way to the point, no longer than the limit and flat", "[ai][steering]") {
    const Vec3 shortStep = ai::clampStep({0.0F, 0.0F, 0.0F}, {0.0F, 0.5F, 2.0F}, 1.0F);
    CHECK(shortStep.x == Approx(0.0F));
    CHECK(shortStep.y == Approx(0.5F));
    CHECK(shortStep.z == 0.0F);
    const Vec3 longStep = ai::clampStep({1.0F, 1.0F, 0.0F}, {4.0F, 5.0F, 0.0F}, 1.0F);
    CHECK(longStep.x == Approx(0.6F));
    CHECK(longStep.y == Approx(0.8F));
}

TEST_CASE("a ray meets a human's disc at its edge, and misses past, behind or beside it", "[ai][steering]") {
    const Vec3 origin{0.0F, 0.0F, 0.0F};
    // Straight at a human 2 m ahead: met 0.63 m short of him.
    const std::optional<float> straight = ai::rayHitsHuman(origin, {0.0F, 3.0F, 0.0F}, {0.0F, 2.0F, 0.0F});
    REQUIRE(straight.has_value());
    CHECK(*straight == Approx(2.0F - ai::kBlockerRadius));
    // Too short to reach him (2 m > 1 m + 0.63 m).
    CHECK_FALSE(ai::rayHitsHuman(origin, {0.0F, 1.0F, 0.0F}, {0.0F, 2.0F, 0.0F}).has_value());
    // Behind the ray.
    CHECK_FALSE(ai::rayHitsHuman(origin, {0.0F, 3.0F, 0.0F}, {0.0F, -0.5F, 0.0F}).has_value());
    // Passing 0.7 m beside him, outside the disc; 0.5 m beside, inside it.
    CHECK_FALSE(ai::rayHitsHuman(origin, {0.0F, 3.0F, 0.0F}, {0.7F, 2.0F, 0.0F}).has_value());
    const std::optional<float> grazing = ai::rayHitsHuman(origin, {0.0F, 3.0F, 0.0F}, {0.5F, 2.0F, 0.0F});
    REQUIRE(grazing.has_value());
    CHECK(*grazing == Approx(2.0F - std::sqrt(0.63F * 0.63F - 0.25F)));
    // Starting inside the disc meets it at once; a ray of no length meets nothing.
    CHECK(ai::rayHitsHuman(origin, {0.0F, 1.0F, 0.0F}, {0.0F, 0.3F, 0.0F}).value_or(-1.0F) == 0.0F);
    CHECK_FALSE(ai::rayHitsHuman(origin, {}, {0.0F, 0.3F, 0.0F}).has_value());
}

TEST_CASE("an AI's predicted step heads for his aim at three quarters of a second of his move", "[ai][steering]") {
    Scene scene;
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    // Standing (no move): no step.
    CHECK(ai::predictedStep(walker).y == Approx(0.0F));
    walker.setMoveAim({40.0F, 50.0F, 0.0F}, 0.0F);
    walker.setMove({0.0F, 1.0F, 0.0F}, 2.0F);
    CHECK(ai::predictedStep(walker).y == Approx(1.5F));
    // Within reach of his aim the step stops at it.
    walker.setMoveAim({40.0F, 40.5F, 0.0F}, 0.0F);
    CHECK(ai::predictedStep(walker).y == Approx(0.5F));
}

TEST_CASE("the list round a walker is a plain radius, the walker left out", "[ai][steering]") {
    Scene scene;
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    Brain& near = scene.add(41.0F, 40.0F, 0.0F);
    scene.add(40.0F, 45.0F, 0.0F);
    Brain& behind = scene.add(40.0F, 38.5F, 0.0F);
    std::vector<Brain*> all;
    for (std::size_t i = 0; i < scene.brains.size(); ++i) {
        all.push_back(&scene.brains.at(i));
    }
    const std::vector<Brain*> list = ai::humansAround(walker, all, 2.0F);
    REQUIRE(list.size() == 2);
    CHECK(list[0] == &near);
    CHECK(list[1] == &behind);
}

TEST_CASE("the blocker is the first disc the relative step meets, never behind or the target", "[ai][steering]") {
    Scene scene;
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    Brain& far = scene.add(40.0F, 42.0F, 0.0F);
    Brain& nearer = scene.add(40.2F, 41.2F, 0.0F);
    Brain& behind = scene.add(40.0F, 39.5F, 0.0F);
    scene.add(43.0F, 41.0F, 0.0F); // well off to the side
    const std::vector<Brain*> list{&far, &nearer, &behind, &scene.brains.at(4)};
    const Vec3 at{40.0F, 40.0F, 0.0F};
    const Vec3 ahead{0.0F, 1.0F, 0.0F};

    const std::optional<ai::Blocker> blocker = ai::findBlocker(walker, list, at, ahead, {0.0F, 3.0F, 0.0F});
    REQUIRE(blocker.has_value());
    CHECK(blocker->brain == &nearer);
    CHECK(blocker->fraction > 0.0F);
    CHECK(blocker->fraction < 1.0F);

    // A step too short reaches nobody.
    CHECK_FALSE(ai::findBlocker(walker, list, at, ahead, {0.0F, 0.2F, 0.0F}).has_value());
    // Standing still, nobody is met.
    CHECK_FALSE(ai::findBlocker(walker, list, at, ahead, {}).has_value());
}

TEST_CASE("a human walking away as fast is no blocker; one walking at the walker is met sooner", "[ai][steering]") {
    Scene scene;
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    Brain& other = scene.add(40.0F, 42.0F, 0.0F);
    const std::vector<Brain*> list{&other};
    const Vec3 at{40.0F, 40.0F, 0.0F};
    const Vec3 ahead{0.0F, 1.0F, 0.0F};
    const Vec3 step{0.0F, 1.5F, 0.0F};

    // Standing: 1.5 m of relative step against a disc 1.37 m out.
    const std::optional<ai::Blocker> standing = ai::findBlocker(walker, list, at, ahead, step);
    REQUIRE(standing.has_value());
    CHECK(standing->fraction == Approx((2.0F - ai::kBlockerRadius) / 1.5F));

    // Walking away at the same speed: no relative step.
    other.setMoveAim({40.0F, 60.0F, 0.0F}, 0.0F);
    other.setMove({0.0F, 1.0F, 0.0F}, 2.0F);
    CHECK_FALSE(ai::findBlocker(walker, list, at, ahead, step).has_value());

    // Walking at him: 3 m of relative step, the same edge met halfway as far along.
    other.setMoveAim({40.0F, 20.0F, 0.0F}, 0.0F);
    const std::optional<ai::Blocker> headOn = ai::findBlocker(walker, list, at, ahead, step);
    REQUIRE(headOn.has_value());
    CHECK(headOn->fraction == Approx((2.0F - ai::kBlockerRadius) / 3.0F));

    // His target is never his blocker.
    walker.setTarget(&other);
    CHECK_FALSE(ai::findBlocker(walker, list, at, ahead, step).has_value());
}

TEST_CASE("giving way starts straight off the mover's path and leans anticlockwise past taken sectors",
          "[ai][steering]") {
    Scene scene;
    Brain& stander = scene.add(40.0F, 40.0F, 0.0F); // facing +y
    // A mover walking up +y 1 m to his right: straight off the path is to the stander's left, sector 2.
    const Vec3 mover{41.0F, 38.0F, 0.0F};
    const Vec3 step{0.0F, 1.5F, 0.0F};
    CHECK(ai::giveWayStart(stander.human(), mover, step) == 2);
    // Nobody round him: he steps into sector 2.
    CHECK(ai::giveWaySector(stander, mover, step) == std::optional<int>{2});
    // Sector 2 taken (a human 1 m to his left): the next is 3.
    scene.add(39.0F, 40.0F, 0.0F);
    stander.update(1000);
    CHECK(ai::giveWaySector(stander, mover, step) == std::optional<int>{3});
}

namespace {

// A steering request for a walker heading for `aim` (its destination too) at `moveSpeed`, arriving within 0.3 m.
ai::SteerRequest walkTo(Vec3 aim, float moveSpeed) {
    return ai::SteerRequest{.moveSpeed = moveSpeed, .aim = aim, .destination = aim, .arrivalRadius = 0.3F};
}

// Sets `brain`'s move toward `aim` at `speed` m/s, as a move action would.
void walking(Brain& brain, Vec3 aim, float speed) {
    brain.setMoveAim(aim, 0.0F);
    const Vec3 way = coney::anim::subtract(aim, brain.human().position());
    brain.setMove(way, speed);
}

} // namespace

TEST_CASE("steering passes a standing human 1 m to the side its line passes, and holds the detour", "[ai][steering]") {
    Scene scene;
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    Brain& stander = scene.add(40.0F, 41.5F, 0.0F);
    const Vec3 aim{40.0F, 50.0F, 0.0F};
    // Dead ahead: E (1 m to the walker's right) against the bearing is 0, so he goes left of him.
    const std::optional<Vec3> steered = ai::steerAroundHumans(walker, walkTo(aim, 2.0F));
    REQUIRE(steered.has_value());
    CHECK(steered->x == Approx(39.0F));
    CHECK(steered->y == Approx(41.5F));
    CHECK(walker.steering().avoiding == &stander);
    CHECK(walker.steering().heldPoint == *steered);
    CHECK(walker.turnBoost() == 1);
    // The fraction of the step at which they touch is the score.
    CHECK(walker.steering().score == Approx((1.5F - ai::kBlockerRadius) / 1.5F));

    // Gone: the detour ends and the turn boost falls back.
    walker.forget(stander);
    CHECK(walker.steering().avoiding == nullptr);
    CHECK(walker.turnBoost() == 0);
}

TEST_CASE("head-on, steering with right of way goes 1 m aside from the contact; without it, it stops for an update",
          "[ai][steering]") {
    Scene scene;
    Brain& first = scene.add(40.0F, 40.0F, 0.0F);
    Brain& second = scene.add(40.0F, 42.0F, 180.0F);
    walking(second, {40.0F, 20.0F, 0.0F}, 2.0F);
    const Vec3 aim{40.0F, 50.0F, 0.0F};
    // A tie of speeds goes to the lower slot: the first has right of way. They would touch 1.37 m into 3 m of
    // relative step; the paths do not cross and he is straight ahead, so the detour is to the walker's left.
    const float t = (2.0F - ai::kBlockerRadius) / 3.0F;
    const std::optional<Vec3> steered = ai::steerAroundHumans(first, walkTo(aim, 2.0F));
    REQUIRE(steered.has_value());
    CHECK(steered->x == Approx(39.0F));
    CHECK(steered->y == Approx(40.0F + (1.5F * t)));
    CHECK(first.steering().contactPoint.y == Approx(40.0F + (1.5F * t)));
    // The point held is the move's own aim (the original's slip, kept).
    CHECK(first.steering().avoiding == &second);
    CHECK(first.steering().heldPoint == aim);

    // The second, without right of way, stops: the aim is kept and the override is 0 for one update.
    walking(first, {40.0F, 60.0F, 0.0F}, 2.0F);
    const Vec3 back{40.0F, 30.0F, 0.0F};
    const std::optional<Vec3> yielded = ai::steerAroundHumans(second, walkTo(back, 2.0F));
    REQUIRE(yielded.has_value());
    CHECK(*yielded == back);
    CHECK(second.steering().avoiding == nullptr);
    CHECK(second.steering().speedOverride() == std::optional<float>{0.0F});
    // The override runs for that one update: the next call counts it out (steering off, so it sets no other).
    second.steering().enabled = false;
    (void)ai::steerAroundHumans(second, walkTo(back, 2.0F));
    CHECK(second.steering().overrideLeft == 0);
}

TEST_CASE("steering behind a slower human going the same way follows without right of way", "[ai][steering]") {
    Scene scene;
    Brain& ahead = scene.add(40.0F, 42.0F, 0.0F);
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    walking(ahead, {40.0F, 60.0F, 0.0F}, 2.0F);
    // Faster by the move (4 against 2 m/s) but not by the humans' speeds: the lower slot has right of way.
    CHECK_FALSE(ai::steerAroundHumans(walker, walkTo({40.0F, 60.0F, 0.0F}, 4.0F)).has_value());
    CHECK(walker.steering().avoiding == nullptr);
    CHECK_FALSE(walker.steering().speedOverride().has_value());
}

TEST_CASE("crossing, steering aims where the other will be when they meet, 1 m to the side he comes from",
          "[ai][steering]") {
    Scene scene;
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    Brain& crosser = scene.add(41.0F, 42.0F, 90.0F);
    walking(crosser, {20.0F, 42.0F, 0.0F}, 2.0F);
    // Steps of (0, 3) and (-1.5, 0): the relative step (1.5, 3) runs straight at him, touching 0.63 m short.
    const float t = (std::sqrt(5.0F) - ai::kBlockerRadius) / std::sqrt(11.25F);
    const std::optional<Vec3> steered = ai::steerAroundHumans(walker, walkTo({40.0F, 60.0F, 0.0F}, 4.0F));
    REQUIRE(steered.has_value());
    CHECK(steered->x == Approx(41.0F - (1.5F * t) + 1.0F));
    CHECK(steered->y == Approx(42.0F));
}

TEST_CASE("two humans heading for the same route node: the one behind follows at 0.75 of the other's speed",
          "[ai][steering]") {
    Scene scene;
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    Brain& other = scene.add(40.0F, 42.0F, 180.0F);
    walking(other, {40.0F, 20.0F, 0.0F}, 2.0F);
    walker.setRouteNode(7);
    other.setRouteNode(7);
    const Vec3 aim{40.0F, 50.0F, 0.0F};
    const std::optional<Vec3> steered = ai::steerAroundHumans(walker, walkTo(aim, 2.0F));
    REQUIRE(steered.has_value());
    CHECK(*steered == aim);
    CHECK(walker.steering().overrideLeft == 5);
    CHECK(walker.steering().avoiding == nullptr);
}

TEST_CASE("steering off to the side of a human going the same way slows for 15 updates without right of way",
          "[ai][steering]") {
    Scene scene;
    Brain& beside = scene.add(40.5F, 40.2F, 0.0F);
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    walking(beside, {40.5F, 60.0F, 0.0F}, 1.0F);
    CHECK_FALSE(ai::steerAroundHumans(walker, walkTo({40.0F, 60.0F, 0.0F}, 4.0F)).has_value());
    CHECK(walker.steering().overrideLeft == 15);
}

TEST_CASE("steering does nothing when off, near the destination or with nobody in the way", "[ai][steering]") {
    Scene scene;
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    scene.add(40.0F, 41.5F, 0.0F);
    CHECK_FALSE(ai::steerAroundHumans(walker, walkTo({40.0F, 40.9F, 0.0F}, 2.0F)).has_value());
    CHECK_FALSE(ai::steerAroundHumans(walker, walkTo({50.0F, 40.0F, 0.0F}, 2.0F)).has_value());
    walker.steering().enabled = false;
    CHECK_FALSE(ai::steerAroundHumans(walker, walkTo({40.0F, 50.0F, 0.0F}, 2.0F)).has_value());
}

TEST_CASE("the side sign, the segment crossing and the corner speed limit", "[ai][steering]") {
    CHECK(ai::sideSign({0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 3.0F, 0.0F}) == 1.0F);
    CHECK(ai::sideSign({0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {-0.1F, 3.0F, 0.0F}) == -1.0F);
    CHECK(ai::segmentsCross({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F}, {2.0F, 0.0F, 0.0F}));
    CHECK_FALSE(ai::segmentsCross({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {0.0F, 2.0F, 0.0F}, {2.0F, 2.0F, 0.0F}));

    // The circle through two points, centred along the axis from the first: square across, half the distance; at
    // 60° off the axis, the full distance; along the facing (square to the axis), none.
    CHECK(ai::cornerRadius({0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}) == Approx(1.0F));
    CHECK(ai::cornerRadius({0.0F, 0.0F, 0.0F}, {1.0F, std::sqrt(3.0F), 0.0F}, {1.0F, 0.0F, 0.0F}) == Approx(2.0F));
    CHECK(ai::cornerRadius({0.0F, 0.0F, 0.0F}, {0.0F, 5.0F, 0.0F}, {1.0F, 0.0F, 0.0F}) == 0.0F);
    CHECK(ai::cornerRadius({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 3.0F}, {1.0F, 0.0F, 0.0F}) == 0.0F);

    Scene scene;
    Brain& runner = scene.add(40.0F, 40.0F, 0.0F);
    const coney::human::Speeds speeds = runner.human().speeds();
    const float run = speeds.run;
    REQUIRE(run > 0.0F);
    // v(g) = 2R tan(w / 2) x 30, w the gait's AI turn per update.
    const auto allowed = [](float radius, float degrees) {
        return 2.0F * radius * std::tan(degrees * 0.5F * 3.14159265F / 180.0F) * 30.0F;
    };
    // Far out to the side (R = 50 m): running's own 4° allows more than the run.
    CHECK(ai::cornerSpeedLimit(runner, {140.0F, 40.0F, 0.0F}, run) == run);
    // 0.3 m beside him (R = 0.15 m): the run allows little; down the gaits the walk's 12° allows the most, and the
    // sneak, capped by its own speed, allows as much again.
    const float beside = ai::cornerSpeedLimit(runner, {40.3F, 40.0F, 0.0F}, run);
    CHECK(beside == Approx(allowed(0.15F, 12.0F)));
    CHECK(beside < run);
    // Dead ahead the radius is 0, and so is the limit; a speed of 0 gives 0.
    CHECK(ai::cornerSpeedLimit(runner, {40.0F, 50.0F, 0.0F}, run) == 0.0F);
    CHECK(ai::cornerSpeedLimit(runner, {40.3F, 40.0F, 0.0F}, 0.0F) == 0.0F);
    // With a turn boost of 1 every rate doubles: the walk now allows more than its own speed, which caps it.
    runner.setTurnBoost(1);
    CHECK(ai::cornerSpeedLimit(runner, {40.3F, 40.0F, 0.0F}, run) ==
          Approx(std::min(speeds.walk, allowed(0.15F, 24.0F))));
}

TEST_CASE("the ground probe refuses a detour point whose first ground below is marked 0x10", "[ai][steering]") {
    std::vector<coney::test::Tri> tris = coney::test::floorAt(0.0F, 0.0F, 10.0F, 0.0F, 10.0F);
    const std::vector<coney::test::Tri> marked =
        coney::test::floorAt(0.0F, 10.0F, 20.0F, 0.0F, 10.0F, 1, ai::kGroundProbeFlag);
    tris.insert(tris.end(), marked.begin(), marked.end());
    const std::unique_ptr<coney::raycast::CollisionMesh> mesh = coney::test::makeMesh(tris);
    CHECK_FALSE(ai::groundProbeBelow(*mesh, {5.0F, 5.0F, 0.0F}));
    CHECK(ai::groundProbeBelow(*mesh, {15.0F, 5.0F, 0.0F}));
    CHECK(ai::groundProbeBelow(*mesh, {15.0F, 5.0F, 1.5F}));
    // More than 2.1 m above it, nothing is met.
    CHECK_FALSE(ai::groundProbeBelow(*mesh, {15.0F, 5.0F, 2.5F}));
    // Off the mesh.
    CHECK_FALSE(ai::groundProbeBelow(*mesh, {25.0F, 5.0F, 0.0F}));
}

TEST_CASE("a move's start seeds the slow counter with the human's index; a new detour keeps the old count",
          "[ai][steering]") {
    Scene scene;
    scene.add(30.0F, 30.0F, 0.0F);
    Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
    Brain& stander = scene.add(40.0F, 41.5F, 0.0F);
    walker.steering().sinceDetour = 7;
    walker.steering().slowDecisions = 3;
    ai::resetAvoidance(walker);
    CHECK(walker.steering().slowDecisions == walker.slot());
    CHECK(walker.steering().sinceDetour == 0);
    CHECK(walker.steering().score == -1e9F);

    REQUIRE(ai::tryDetour(walker, {39.0F, 41.5F, 0.0F}, stander, 0.5F));
    walker.steering().sinceDetour = 5;
    REQUIRE(ai::tryDetour(walker, {41.0F, 41.5F, 0.0F}, stander, 0.25F));
    CHECK(walker.steering().sinceDetour == 5);
    CHECK(walker.steering().heldPoint == Vec3{41.0F, 41.5F, 0.0F});
    ai::clearSteering(walker);
    CHECK(walker.steering().sinceDetour == 0);
    CHECK(walker.steering().avoiding == nullptr);
}

TEST_CASE("at a shared route node only the human farther from it is held and follows; the nearer one steers",
          "[ai][steering]") {
    const auto request = [](Vec3 node) {
        return ai::SteerRequest{
            .moveSpeed = 2.0F, .aim = node, .destination = Vec3{40.0F, 60.0F, 0.0F}, .arrivalRadius = 0.0F};
    };
    {
        // The node is nearer him: I am held and follow at 0.75 of his speed.
        Scene scene;
        Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
        scene.add(40.0F, 41.0F, 0.0F);
        walker.setRouteNode(7);
        scene.brains.at(1).setRouteNode(7);
        const Vec3 node{40.0F, 41.2F, 0.0F};
        const std::optional<Vec3> steered = ai::steerAroundHumans(walker, request(node));
        REQUIRE(steered.has_value());
        CHECK(*steered == node);
        CHECK(walker.steering().overrideLeft == 5);
        CHECK(walker.steering().avoiding == nullptr);
    }
    {
        // The node is nearer me: I am not held, and pass him as a stander.
        Scene scene;
        Brain& walker = scene.add(40.0F, 40.0F, 0.0F);
        Brain& stander = scene.add(40.0F, 41.0F, 0.0F);
        walker.setRouteNode(7);
        stander.setRouteNode(7);
        const std::optional<Vec3> steered = ai::steerAroundHumans(walker, request({40.0F, 40.45F, 0.0F}));
        REQUIRE(steered.has_value());
        CHECK(walker.steering().overrideLeft == 0);
        CHECK(walker.steering().avoiding == &stander);
    }
}
