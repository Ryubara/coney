// SPDX-License-Identifier: GPL-3.0-or-later
// The move action over path data (docs/research/ai-moving.md#move-action), the turn actions and ActLookAt
// (docs/research/ai-scripting.md#look-at), GoalMoveToFlag (docs/research/ai-scripting.md#move-to-flag) and the scripts'
// hold on brains. Synthetic clips, a flat floor and synthetic path data; game time is the steps run (1/30 s each).
#include "ai/move_action.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/action.h"
#include "ai/ai_config.h"
#include "ai/attack_action.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/move_to_flag_goal.h"
#include "ai/pedestrian_goal.h"
#include "ai/play_dyn_animation_goal.h"
#include "ai/route_planner.h"
#include "ai/scripted_brains.h"
#include "ai/turn_action.h"
#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "human/human.h"
#include "human/humans.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"
#include "support/path_fixtures.h"
#include "warriors/created_humans.h"
#include "world_objects/flags.h"

using coney::ai::Brain;
using coney::ai::gaitSpeed;
using coney::ai::MoveAction;
using coney::ai::MoveFailure;
using coney::ai::MoveRequest;
using coney::anim::Vec3;
using coney::human::Human;

namespace {

// Degrees to radians.
float radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0F; }

// The distance in plan between two points.
float planDistance(Vec3 a, Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// AI humans of the synthetic character on a floor over [0, 80]², with path data `map` (the U at (40, 40) unless
// given) and its planner given to every brain.
struct MoveScene {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh;
    coney::world::PathMap map;
    coney::ai::RoutePlanner planner{map};
    std::vector<std::unique_ptr<Human>> humans;
    coney::human::Humans step;
    coney::ai::Brains brains;

    explicit MoveScene(coney::world::PathMap paths = coney::test::uCorridors(40.0F, 40.0F),
                       const std::vector<coney::test::Tri>& extra = {})
        : mesh(coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), extra))),
          map(std::move(paths)) {
        step.setBrains(brains.hook());
        brains.setPlanner(&planner);
    }

    // A human with its feet at `feet` facing `headingDegrees` (0 faces +y, 90 faces -x), with a brain of `type` (a
    // gang brain unless given), driven by a pad when `padControlled`.
    Brain& add(Vec3 feet, float headingDegrees, coney::ai::BrainType type = coney::ai::BrainType::Gang,
               bool padControlled = false) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        made.setFighterProfile(
            coney::human::FighterProfile{.player = false, .powerClass = coney::ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), feet, headingDegrees);
        step.add(made, padControlled);
        return brains.add(made, type, coney::ai::FightSettings{}, static_cast<std::uint32_t>(humans.size()));
    }

    // A player (fighter profile `player`) with the player's brain, his pad driving him, at `feet` facing
    // `headingDegrees`.
    Brain& addPlayer(Vec3 feet, float headingDegrees) {
        Brain& brain = add(feet, headingDegrees, coney::ai::BrainType::Player, true);
        brain.human().setFighterProfile(
            coney::human::FighterProfile{.player = true, .powerClass = coney::ai::kWarriorPowerClass, .health = 1400});
        return brain;
    }

    // A punch from `brain`'s human makes the nearest foe ahead his fight target; the attack then plays out.
    void lockOn(Brain& brain) {
        brain.human().record().command = coney::combat::command::kSquarePressed;
        step.update(mesh.get());
        brain.human().record().command = coney::combat::command::kNone;
        run(60);
    }

    // Steps until `done` holds or `limit` steps have run; calls `each` after every step. Returns the steps run.
    template <typename Done, typename Each> int runUntil(int limit, Done done, Each each) {
        for (int k = 1; k <= limit; ++k) {
            step.update(mesh.get());
            each();
            if (done()) {
                return k;
            }
        }
        return limit;
    }
    void run(int steps) {
        for (int k = 0; k < steps; ++k) {
            step.update(mesh.get());
        }
    }
};

// A move the test sets, written into the brain every update as the original's actions write theirs: the brain's
// update clears the move before its goals and actions run (docs/research/characters.md#ai-locomotion).
struct HeldMove {
    float heading = 0.0F;    // radians, 0 facing +y
    float speed = 0.0F;      // m/s
    std::optional<Vec3> aim; // the point (+0x90) the stance walk strafes to; none leaves the brain's
};

// The action that writes a HeldMove each update and never ends.
class HoldMoveAction final : public coney::ai::Action {
  public:
    explicit HoldMoveAction(const HeldMove& move) : m_move(&move) {}

    // Writes the move this update.
    [[nodiscard]] coney::ai::ActionStatus update(Brain& brain) override {
        if (m_move->aim) {
            brain.setMoveAim(*m_move->aim, 0.0F);
        }
        brain.setMoveHeading(m_move->heading, m_move->speed);
        return coney::ai::ActionStatus::Running;
    }

  private:
    const HeldMove* m_move;
};

// The front action as a move; null when it is not one.
MoveAction* frontMove(Brain& brain) { return dynamic_cast<MoveAction*>(brain.frontAction()); }

// A move to `point` within `radius` at `gait` (2 walks), starting at once.
std::unique_ptr<MoveAction> moveTo(Vec3 point, float radius, int gait = 2) {
    return std::make_unique<MoveAction>(
        MoveRequest{.point = point, .radius = radius, .gait = gait, .option = false, .delayMs = 0, .flagKind = false});
}

// GoalMoveToFlag's call: `human` walks to `flag` within `radius`, turning to its heading when `faceFlag`.
coney::script::MoveToFlagCall moveToFlagCall(double human, double flag, float radius, bool faceFlag) {
    coney::script::MoveToFlagCall call;
    call.human = human;
    call.flag = flag;
    call.gait = 2;
    call.radius = radius;
    call.faceFlag = faceFlag;
    return call;
}

// The move-to-flag goal's order: walk within `radius` of the point `distance` from `flag` towards `angle` degrees.
coney::ai::MoveToFlagOrder flagOrder(double flag, float radius, float angle = 0.0F, float distance = 0.0F) {
    coney::ai::MoveToFlagOrder order;
    order.flag = flag;
    order.radius = radius;
    order.angleDegrees = angle;
    order.distance = distance;
    return order;
}

// ActLookAt's call: `human` turns to `target` with the turn value `turn` and the default (random) start delay.
coney::script::LookAtCall lookAtCall(double human, double target, float turn) {
    coney::script::LookAtCall call;
    call.human = human;
    call.target = target;
    call.turn = turn;
    return call;
}

} // namespace

TEST_CASE("a gait's speed is the human's own clip speed, and 0 for gait 0 or any unknown gait", "[ai][move]") {
    const coney::human::Speeds speeds{
        .base = 3.4F, .sneak = 1.5F, .walk = 1.6F, .jog = 4.8F, .run = 7.8F, .sprint = 10.2F};
    CHECK(gaitSpeed(speeds, 1) == 1.5F);
    CHECK(gaitSpeed(speeds, 2) == 1.6F);
    CHECK(gaitSpeed(speeds, 3) == 4.8F);
    CHECK(gaitSpeed(speeds, 4) == 7.8F);
    CHECK(gaitSpeed(speeds, 5) == 10.2F);
    CHECK(gaitSpeed(speeds, 0) == 0.0F);
    CHECK(gaitSpeed(speeds, 6) == 0.0F);
    CHECK(gaitSpeed(speeds, -1) == 0.0F);
}

TEST_CASE("a move goes straight to its point when the line is walkable and stops within its radius", "[ai][move]") {
    MoveScene scene;
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, -90.0F);
    brain.queueAction(moveTo({49.0F, 41.0F, 0.0F}, 0.3F));
    scene.run(1);
    REQUIRE(frontMove(brain) != nullptr);
    CHECK_FALSE(frontMove(brain)->routed());
    CHECK(brain.moveAim() == Vec3{49.0F, 41.0F, 0.0F});
    scene.runUntil(600, [&] { return brain.actionCount() == 0; }, [] {});
    CHECK(brain.actionCount() == 0);
    CHECK(brain.moveFailure() == MoveFailure::None);
    CHECK_FALSE(brain.human().record().move.has_value());
    CHECK(planDistance(brain.human().position(), {49.0F, 41.0F, 0.0F}) < 0.4F);
}

TEST_CASE("a move round the U follows its route's waypoints and stays on the polygons", "[ai][move]") {
    MoveScene scene;
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, -90.0F);
    brain.queueAction(moveTo({41.0F, 49.0F, 0.0F}, 0.3F));
    scene.run(1);
    REQUIRE(frontMove(brain) != nullptr);
    CHECK(frontMove(brain)->routed());
    float worstOff = 0.0F;
    const int steps = scene.runUntil(
        1200, [&] { return brain.actionCount() == 0; },
        [&] {
            const Vec3 at = brain.human().position();
            if (!scene.map.polygonAt(at.x, at.y)) {
                worstOff = 1.0F;
            }
        });
    CHECK(steps < 1200);
    CHECK(worstOff == 0.0F);
    CHECK(brain.moveFailure() == MoveFailure::None);
    CHECK(planDistance(brain.human().position(), {41.0F, 49.0F, 0.0F}) < 0.4F);
    CHECK(scene.planner.routesInUse() == 0);
}

TEST_CASE("running into a tight corner, the move slows to a speed that keeps it on the polygons", "[ai][move]") {
    // An L whose corner leaves 0.75 m beyond its node on the way in: at the run the turn would swing out of it. The
    // side leaves 3.25 m: past the waypoint the move asks for the run again and the human speeds up while still turning
    // at the run's 4° an update, swinging out about 3 m (docs/research/ai-moving.md#move-action).
    coney::test::PathBuilder builder;
    const std::uint32_t bottom = builder.rectangle(40.0F, 49.5F, 40.0F, 42.0F);
    const std::uint32_t side = builder.rectangle(48.0F, 52.0F, 40.0F, 54.0F);
    builder.node(bottom, 42.0F, 41.0F);
    builder.node(bottom, 48.75F, 41.0F);
    builder.node(side, 48.75F, 52.0F);
    builder.link(0, 1);
    builder.link(1, 2);
    MoveScene scene(builder.build());
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, -90.0F);
    brain.queueAction(moveTo({48.75F, 53.0F, 0.0F}, 0.3F, 4));
    const float run = brain.human().speeds().run;
    float slowest = run;
    bool off = false;
    scene.runUntil(
        900, [&] { return brain.actionCount() == 0; },
        [&] {
            const std::optional<coney::human::BrainMove> move = brain.human().record().move;
            if (move && move->speed > 0.0F) {
                slowest = std::min(slowest, move->speed);
            }
            const Vec3 at = brain.human().position();
            off = off || !scene.map.polygonAt(at.x, at.y);
        });
    CHECK(brain.actionCount() == 0);
    CHECK(slowest < run);
    CHECK(slowest >= brain.human().speeds().walk);
    CHECK_FALSE(off);
}

TEST_CASE("standing far off its way, a move turns on the spot before it walks", "[ai][move]") {
    MoveScene scene;
    Brain& brain = scene.add({45.0F, 41.0F, 0.0F}, -90.0F); // facing +x, the point behind it
    brain.queueAction(moveTo({41.0F, 41.0F, 0.0F}, 0.3F));
    scene.run(1);
    const std::optional<coney::human::BrainMove> first = brain.human().record().move;
    REQUIRE(first.has_value());
    if (!first) {
        return;
    }
    CHECK(first.value().speed == 0.0F);
    CHECK(std::fabs(coney::human::wrapAngle(first.value().heading - radians(90.0F))) < 1e-3F);
    // It turns where it stands until it is within 30 degrees, then walks.
    const Vec3 start = brain.human().position();
    scene.runUntil(60, [&] { return brain.human().record().move && brain.human().record().move->speed > 0.0F; }, [] {});
    CHECK(planDistance(brain.human().position(), start) < 0.05F);
    CHECK(std::fabs(coney::human::wrapAngle(brain.human().heading() - radians(90.0F))) <=
          coney::ai::kTurnOnSpotAngle + 1e-3F);
}

TEST_CASE("a move that covers less than 0.2 m in 60 updates is stuck", "[ai][move]") {
    // A wall across the bottom corridor that the path data does not know about.
    MoveScene scene(coney::test::uCorridors(40.0F, 40.0F),
                    coney::test::wallFacingMinusX(44.0F, 39.0F, 43.0F, -1.0F, 3.0F));
    Brain& brain = scene.add({42.5F, 41.0F, 0.0F}, -90.0F);
    brain.queueAction(moveTo({49.0F, 41.0F, 0.0F}, 0.3F));
    scene.runUntil(600, [&] { return brain.actionCount() == 0; }, [] {});
    CHECK(brain.actionCount() == 0);
    CHECK(brain.moveFailure() == MoveFailure::Stuck);
    CHECK(brain.human().position().x < 44.0F);
}

TEST_CASE("a move to a point off the path data fails at its start; with no path data it goes straight", "[ai][move]") {
    MoveScene scene;
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, 0.0F);
    brain.queueAction(moveTo({41.0F, 60.0F, 0.0F}, 0.3F));
    scene.run(1);
    CHECK(brain.actionCount() == 0);
    CHECK(brain.moveFailure() == MoveFailure::NoRoute);

    scene.brains.setPlanner(nullptr);
    brain.queueAction(moveTo({41.0F, 49.0F, 0.0F}, 0.3F));
    scene.runUntil(600, [&] { return brain.actionCount() == 0; }, [] {});
    CHECK(brain.moveFailure() == MoveFailure::None);
    CHECK(planDistance(brain.human().position(), {41.0F, 49.0F, 0.0F}) < 0.4F);
}

TEST_CASE("a look-at turns the human until it faces its target within 15 degrees, then ends", "[ai][turn]") {
    MoveScene scene;
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, 0.0F);
    const coney::ai::TargetLocator locate = [](double) { return std::optional<Vec3>{Vec3{45.0F, 41.0F, 0.0F}}; };
    brain.queueAction(coney::ai::TurnAction::lookAt(locate, 7.0, 0.5F, 0));
    const int steps = scene.runUntil(120, [&] { return brain.actionCount() == 0; }, [] {});
    CHECK(steps < 30);
    CHECK(std::fabs(coney::human::wrapAngle(brain.human().heading() - radians(-90.0F))) <= coney::ai::kTurnDoneAngle);
    CHECK_FALSE(brain.human().record().move.has_value());
    CHECK(planDistance(brain.human().position(), {41.0F, 41.0F, 0.0F}) < 0.01F);
}

TEST_CASE("a turn gives up after 3 s, ends when its target goes, and waits out its start delay", "[ai][turn]") {
    MoveScene scene;
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, 0.0F);
    // A target that jumps from one side to the other each update is never faced.
    int calls = 0;
    const coney::ai::TargetLocator dodging = [&calls](double) {
        return std::optional<Vec3>{Vec3{(++calls % 2) == 0 ? 45.0F : 37.0F, 41.0F, 0.0F}};
    };
    brain.queueAction(coney::ai::TurnAction::lookAt(dodging, 7.0, 0.5F, 0));
    const int steps = scene.runUntil(200, [&] { return brain.actionCount() == 0; }, [] {});
    CHECK(steps >= 90);
    CHECK(steps <= 93);

    brain.queueAction(coney::ai::TurnAction::lookAt([](double) { return std::optional<Vec3>{}; }, 7.0, 0.5F, 0));
    scene.run(1);
    CHECK(brain.actionCount() == 0);

    // The fourth argument of ActLookAt is a start delay: 500 ms is 15 updates.
    brain.queueAction(coney::ai::TurnAction::lookAt(
        [](double) { return std::optional<Vec3>{Vec3{45.0F, 41.0F, 0.0F}}; }, 7.0, 0.5F, 500));
    scene.run(14);
    REQUIRE(brain.frontAction() != nullptr);
    CHECK_FALSE(brain.frontAction()->started());
    scene.run(2);
    CHECK((brain.frontAction() == nullptr || brain.frontAction()->started()));
}

TEST_CASE("GoalMoveToFlag walks to the flag, turns to its heading, arrives and tells the flag", "[ai][flag]") {
    MoveScene scene;
    coney::world_objects::WorldFlags flags;
    flags.createPool(4);
    const double flag = flags.add(100.0, "Spot", {48.0F, 41.0F, 0.0F}, 90.0F).handle;
    coney::ai::ScriptedBrains scripted(scene.brains, flags);
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, 0.0F);
    scripted.bind(1.0, brain);
    scripted.goalMoveToFlag(moveToFlagCall(1.0, flag, 0.4F, true));
    REQUIRE(brain.topGoal() != nullptr);
    CHECK(brain.topGoal()->type() == coney::ai::GoalType::MoveToFlag);
    scene.runUntil(900, [&] { return brain.goalCount() == 0; }, [] {});
    CHECK(brain.goalCount() == 0);
    CHECK(scripted.arrivals() == 1);
    CHECK(planDistance(brain.human().position(), {48.0F, 41.0F, 0.0F}) <= 0.45F);
    CHECK(std::fabs(coney::human::wrapAngle(brain.human().heading() - radians(90.0F))) <= coney::ai::kFaceFlagAngle);
}

TEST_CASE("a player under script control turns on the spot to the flag's heading and arrives (the workouts' walk)",
          "[ai][flag]") {
    // worksetup.StartWorkout: DeactivateWarchief (BrDead(player, true): the pad given up, the goals run), then
    // GoalMoveToFlag(player, fWorkout[k], walk, 0, 0, 0.5, 0, faceFlag = true). The player already stands inside the
    // radius, facing -90 degrees, and the flag faces 93 (the heavy bag's): only the turn is left. The bag, a human
    // 0.7 m ahead of him, is his fight target, so he stands in the fight stance locked on it
    // (docs/research/workouts.md#coney).
    MoveScene scene;
    coney::world_objects::WorldFlags flags;
    flags.createPool(4);
    const double flag = flags.add(100.0, "fWorkout", {41.2F, 41.0F, 0.0F}, 93.0F).handle;
    coney::ai::ScriptedBrains scripted(scene.brains, flags);
    Brain& player = scene.addPlayer({41.0F, 41.0F, 0.0F}, -90.0F);
    scene.add({41.7F, 41.0F, 0.0F}, 90.0F);
    scene.lockOn(player);
    REQUIRE(player.human().fighter().lockTarget() != nullptr);
    // DeactivateWarchief: the pad given up.
    scene.step.setPadControlled(player.human(), false);
    player.setDead(true);
    scripted.bind(1.0, player);
    scripted.goalMoveToFlag(moveToFlagCall(1.0, flag, 0.5F, true));
    // The 177 degrees at the standing step, then message 8, well within worksetup.TimeOut's 5 s (in the original the
    // walk to the flag and the turn took 12 updates, docs/research/workouts.md#start).
    const int steps = scene.runUntil(150, [&] { return scripted.arrivals() == 1; }, [] {});
    CHECK(scripted.arrivals() == 1);
    CHECK(steps < 30);
    CHECK(std::fabs(coney::human::wrapAngle(player.human().heading() - radians(93.0F))) <= coney::ai::kFaceFlagAngle);
}

TEST_CASE("locked on a foe, a pad's human faces it; a player under script control follows his brain", "[ai][turn]") {
    // Human_FightStanceMove and Human_UpdateControl (docs/research/ai.md#fight-facing, characters.md#ai-locomotion):
    // the pad's player faces his lock target each update; a player whose pad a script took runs the AI's stance
    // control, so he turns by the brain's heading: 24 degrees an update asked to move, the standing step (12) stopped
    // more than 15 degrees off, nothing within 15 degrees or with no move asked.
    MoveScene scene;
    Brain& player = scene.addPlayer({41.0F, 41.0F, 0.0F}, -90.0F);
    Brain& foe = scene.add({42.0F, 41.0F, 0.0F}, 90.0F);
    scene.lockOn(player);
    scene.lockOn(foe);
    REQUIRE(player.human().fighter().lockTarget() != nullptr);
    REQUIRE(foe.human().fighter().lockTarget() != nullptr);
    const float toFoe = radians(-90.0F); // +x
    const auto off = [](const Brain& brain, float heading) {
        return std::fabs(coney::human::wrapAngle(brain.human().heading() - heading));
    };

    SECTION("the pad's player snaps back to face his target") {
        player.human().place(player.human().position(), radians(90.0F));
        scene.run(1);
        CHECK(off(player, toFoe) < 1e-3F);
    }
    SECTION("a player under script control turns by his brain's heading") {
        scene.step.setPadControlled(player.human(), false);
        player.setDead(true);
        // No move asked: he keeps his facing.
        player.human().place(player.human().position(), radians(0.0F));
        scene.run(1);
        CHECK(off(player, radians(0.0F)) < 1e-3F);
        // Stopped, 90 degrees off: the standing step of 12 degrees an update. The brain's update clears the move, so
        // an action writes it each update.
        const HeldMove held{.heading = radians(90.0F), .speed = 0.0F, .aim = std::nullopt};
        REQUIRE(player.queueAction(std::make_unique<HoldMoveAction>(held)));
        scene.run(1);
        CHECK(off(player, radians(12.0F)) < 1e-3F);
        // Stopped within 15 degrees: nothing turns.
        player.human().place(player.human().position(), radians(80.0F));
        scene.run(1);
        CHECK(off(player, radians(80.0F)) < 1e-3F);
        // Wounded he turns even stopped within 15 degrees, by the wounded 0.375 degrees.
        player.human().script().wounded = true;
        scene.run(1);
        CHECK(off(player, radians(80.375F)) < 1e-3F);
    }
}

TEST_CASE("GoalMoveToFlag aims at an offset from the flag, ends when its flag goes, and plans again after a short move",
          "[ai][flag]") {
    MoveScene scene;
    coney::world_objects::WorldFlags flags;
    flags.createPool(4);
    const double flag = flags.add(100.0, "Spot", {42.0F, 41.0F, 0.0F}, 0.0F).handle;
    coney::ai::ScriptedBrains scripted(scene.brains, flags);
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, 0.0F);
    // 2 m at 0 degrees: along (cos 0, sin 0), +x.
    coney::ai::goalMoveToFlag(brain, flagOrder(flag, 0.3F, 0.0F, 2.0F), scripted);
    scene.run(1);
    const auto* goal = dynamic_cast<const coney::ai::MoveToFlagGoal*>(brain.topGoal());
    REQUIRE(goal != nullptr);
    CHECK(planDistance(goal->target(), {44.0F, 41.0F, 0.0F}) < 1e-4F);
    // A negative distance is not special: -1 m at -1 degrees, as level99's Vermin is sent (docs/research/ai.md).
    Brain& other = scene.add({41.0F, 45.0F, 0.0F}, 0.0F);
    coney::ai::goalMoveToFlag(other, flagOrder(flag, 0.5F, -1.0F, -1.0F), scripted);
    scene.run(1);
    const auto* offset = dynamic_cast<const coney::ai::MoveToFlagGoal*>(other.topGoal());
    REQUIRE(offset != nullptr);
    CHECK(planDistance(offset->target(), {41.00015F, 41.01745F, 0.0F}) < 1e-4F);
    // Each move it queues ends short when cut, and the goal queues another.
    REQUIRE(brain.actionCount() == 1);
    brain.clearActions();
    scene.run(1);
    CHECK(frontMove(brain) != nullptr);

    // A flag no longer there: the goal ends without arriving.
    brain.flush();
    coney::ai::goalMoveToFlag(brain, flagOrder(999.0, 0.3F), scripted);
    scene.run(1);
    CHECK(brain.goalCount() == 0);
    CHECK(scripted.arrivals() == 0);
}

TEST_CASE("the scripts' brains take GoalMoveToFlag and ActLookAt by handle and ignore other handles", "[ai][flag]") {
    MoveScene scene;
    coney::world_objects::WorldFlags flags;
    flags.createPool(4);
    const double flag = flags.add(100.0, "Spot", {45.0F, 41.0F, 0.0F}, 0.0F).handle;
    coney::ai::ScriptedBrains scripted(scene.brains, flags);
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, 0.0F);
    Brain& other = scene.add({43.0F, 41.0F, 0.0F}, 0.0F);
    scripted.bind(1.0, brain);
    scripted.bind(2.0, other);
    CHECK(scripted.locate(2.0) == other.human().position());
    CHECK(scripted.locate(flag) == Vec3{45.0F, 41.0F, 0.0F});
    CHECK_FALSE(scripted.locate(77.0).has_value());

    scripted.actLookAt(lookAtCall(1.0, 2.0, 0.5F));
    const auto* turn = dynamic_cast<const coney::ai::TurnAction*>(brain.frontAction());
    REQUIRE(turn != nullptr);
    CHECK(turn->delayMs() == coney::ai::kRandomDelay);
    CHECK(turn->turn() == 0.5F);

    scripted.goalMoveToFlag(moveToFlagCall(3.0, flag, 0.3F, false));
    scripted.actLookAt(lookAtCall(3.0, 1.0, 0.0F));
    CHECK(other.goalCount() == 0);
    CHECK(other.actionCount() == 0);
    scripted.unbind(2.0);
    CHECK(scripted.brain(2.0) == nullptr);
}

TEST_CASE("the scripts' brains hold calls until the level makes the humans, then run them in order", "[ai][flag]") {
    MoveScene scene;
    coney::world_objects::WorldFlags flags;
    flags.createPool(4);
    coney::ai::ScriptedBrains scripted(scene.brains, flags);
    scripted.hold();
    coney::HumanCreation ash;
    ash.name = "Ash";
    ash.type = 40;
    ash.position = std::array<float, 3>{41.0F, 41.0F, 0.0F};
    ash.handle = 7.0;
    scripted.humanCreated(ash);
    scripted.brSuspend(7.0, true);
    scripted.humanTeleported(
        7.0, coney::world_objects::Placement{.position = {45.0F, 41.0F, 0.0F}, .headingDegrees = 30.0F});
    CHECK(scripted.holding());
    CHECK(scripted.held() == 3);
    CHECK(scripted.brain(7.0) == nullptr);

    std::vector<std::string> made;
    scripted.release([&](const coney::HumanCreation& human) -> Brain* {
        made.push_back(human.name);
        return &scene.add({human.position->at(0), human.position->at(1), human.position->at(2)}, human.headingDegrees);
    });
    CHECK_FALSE(scripted.holding());
    CHECK(made == std::vector<std::string>{"Ash"});
    Brain* brain = scripted.brain(7.0);
    REQUIRE(brain != nullptr);
    CHECK(brain->handle() == 7.0);
    CHECK(brain->characterClass() == 40);
    CHECK(brain->suspended());
    CHECK(brain->services() == &scripted);
    // Teleported to the flag, and found there by its handle.
    const coney::world_objects::Placement at = scripted.humanPlacement(7.0).value_or(coney::world_objects::Placement{});
    REQUIRE(scripted.humanPlacement(7.0).has_value());
    CHECK(std::fabs(at.position[0] - 45.0F) < 1e-3F);
    CHECK(std::fabs(at.headingDegrees - 30.0F) < 1e-2F);

    // Once released, calls run at once.
    scripted.brSuspend(7.0, false);
    CHECK_FALSE(brain->suspended());
}

namespace {

// Two flags 5 m apart along the U's bottom corridor, found by handle.
class TwoFlags final : public coney::ai::FlagServices {
  public:
    [[nodiscard]] std::optional<coney::world_objects::Placement> flag(double handle) const override {
        if (handle == 1) {
            return coney::world_objects::Placement{.position = {44.0F, 41.0F, 0.0F}};
        }
        if (handle == 2) {
            return coney::world_objects::Placement{.position = {49.0F, 41.0F, 0.0F}};
        }
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("a pedestrian walks to the nearest network node, then on along its links", "[ai][move]") {
    MoveScene scene;
    TwoFlags flags;
    coney::world_objects::FlagNet net;
    REQUIRE(net.add({.flag = 2, .links = {1, 0, 0, 0}}));
    REQUIRE(net.add({.flag = 1, .links = {2, 0, 0, 0}}));
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, -90.0F);
    REQUIRE(
        brain.pushGoal(std::make_unique<coney::ai::PedestrianGoal>(coney::ai::PedestrianOrder{.mode = 1}, net, flags)));
    scene.run(2);
    const auto* goal = dynamic_cast<const coney::ai::PedestrianGoal*>(brain.topGoal());
    REQUIRE(goal != nullptr);
    CHECK(goal->heading() == std::optional<double>{1.0});
    // It reaches flag 1, then heads for flag 2 (its only link), and back.
    const int steps = scene.runUntil(900, [&] { return goal->heading() == std::optional<double>{2.0}; }, [] {});
    CHECK(steps < 900);
    CHECK(planDistance(brain.human().position(), {44.0F, 41.0F, 0.0F}) < 1.2F);
    scene.runUntil(900, [&] { return goal->heading() == std::optional<double>{1.0}; }, [] {});
    CHECK(planDistance(brain.human().position(), {49.0F, 41.0F, 0.0F}) < 1.2F);
    CHECK(coney::ai::pedestrianGait(2) == 3);
    CHECK(coney::ai::pedestrianGait(7) == 2);
}

TEST_CASE("GoalPlayDynAnimation plays the named level clip as anim 668, holding the human until it ends",
          "[ai][scripted]") {
    MoveScene scene;
    coney::world_objects::WorldFlags flags;
    coney::ai::ScriptedBrains scripted(scene.brains, flags);
    // The level's dynamic clips: here the synthetic walk clip under the name "wave".
    const coney::anim::AnimClip* wave = scene.character.anims.clip(408);
    REQUIRE(wave != nullptr);
    scripted.setClipSource([wave](std::string_view name) { return name == "wave" ? wave : nullptr; });
    Brain& brain = scene.add({41.0F, 41.0F, 0.0F}, 0.0F);
    scripted.bind(1.0, brain);
    REQUIRE(coney::ai::goalPlayDynAnimation(brain, scripted, "wave", "", true));
    // It starts within a few updates and plays as anim 668, the human busy while it does.
    bool played = false;
    const int steps = scene.runUntil(
        300, [&] { return brain.goalCount() == 0; },
        [&] {
            if (brain.human().animator().animId() == static_cast<std::uint32_t>(coney::ai::kDynamicAnimId)) {
                played = true;
                CHECK(coney::human::stickBusy(brain.human().gateInput()));
            }
        });
    CHECK(played);
    CHECK(brain.goalCount() == 0);
    // It took about the clip's length, not a single update.
    CHECK(static_cast<float>(steps) >= wave->duration * 30.0F * 0.9F);

    // A name no clip answers plays nothing and still ends at once.
    REQUIRE(coney::ai::goalPlayDynAnimation(brain, scripted, "missing", "", true));
    CHECK(scene.runUntil(10, [&] { return brain.goalCount() == 0; }, [] {}) < 10);
}

TEST_CASE("a route jump's arc is the first vertical speed that comes down on the point at 10 m/s or less across",
          "[ai][move]") {
    // A drop of 2 m takes sqrt(2 / 7.84) s; a quadratic with no real root gives 0.
    CHECK(std::fabs(coney::ai::largerRoot(coney::ai::kHalfGravity, 0.0F, -2.0F) - std::sqrt(2.0F / 7.84F)) < 1e-5F);
    CHECK(coney::ai::largerRoot(1.0F, 0.0F, 1.0F) == 0.0F);
    CHECK(coney::ai::largerRoot(0.0F, 2.0F, -4.0F) == 2.0F);
    // 4.12 m across, 0.05 m up: 0.5 and 1.25 m/s never reach the height, 2.0 and 2.75 land too fast across; 3.5 m/s
    // comes down in 0.4317 s, 9.546 m/s across.
    const std::optional<coney::anim::Vec3> arc = coney::ai::routeJumpVelocity({4.12F, 0.0F, 0.05F});
    REQUIRE(arc.has_value());
    if (!arc) {
        return;
    }
    CHECK(arc->z == 3.5F);
    CHECK(std::fabs(arc->x - 9.546F) < 0.01F);
    CHECK(arc->y == 0.0F);
    // Too far for any speed up to 5.75 m/s: nothing.
    CHECK_FALSE(coney::ai::routeJumpVelocity({30.0F, 0.0F, 0.0F}).has_value());
}

TEST_CASE("a running AI turns 4 degrees an update, 8 with a turn boost of 1, and never eases", "[ai][move]") {
    MoveScene scene;
    Brain& brain = scene.add({20.0F, 20.0F, 0.0F}, 0.0F);
    const float run = brain.human().speeds().run;
    // Up to a run along +y, then asked to run along -x.
    HeldMove held{.heading = 0.0F, .speed = run, .aim = std::nullopt};
    REQUIRE(brain.queueAction(std::make_unique<HoldMoveAction>(held)));
    scene.run(60);
    REQUIRE(brain.human().gait() == coney::human::Gait::Run);
    const auto turnSteps = [&](int boost) {
        brain.setTurnBoost(boost);
        const float before = brain.human().heading();
        held.heading = radians(90.0F);
        scene.runUntil(1, [] { return true; }, [] {});
        const float first = coney::human::wrapAngle(brain.human().heading() - before);
        const float mid = brain.human().heading();
        scene.runUntil(1, [] { return true; }, [] {});
        const float second = coney::human::wrapAngle(brain.human().heading() - mid);
        return std::pair{first, second};
    };
    const auto [a, b] = turnSteps(0);
    CHECK(a == Catch::Approx(radians(4.0F)).margin(1e-4));
    CHECK(b == Catch::Approx(radians(4.0F)).margin(1e-4));
    const auto [c, d] = turnSteps(1);
    CHECK(c == Catch::Approx(radians(8.0F)).margin(1e-4));
    CHECK(d == Catch::Approx(radians(8.0F)).margin(1e-4));
}

TEST_CASE("a move steers round a human standing on its line and still arrives", "[ai][move][steering]") {
    MoveScene scene;
    Brain& walker = scene.add({41.0F, 41.0F, 0.0F}, -90.0F);
    Brain& stander = scene.add({45.0F, 41.0F, 0.0F}, 0.0F);
    walker.queueAction(moveTo({49.0F, 41.0F, 0.0F}, 0.3F));
    float nearest = 100.0F;
    float widest = 0.0F;
    scene.runUntil(
        600, [&] { return walker.actionCount() == 0; },
        [&] {
            nearest = std::min(nearest, planDistance(walker.human().position(), stander.human().position()));
            widest = std::max(widest, std::fabs(walker.human().position().y - 41.0F));
        });
    CHECK(walker.actionCount() == 0);
    CHECK(walker.moveFailure() == MoveFailure::None);
    CHECK(planDistance(walker.human().position(), {49.0F, 41.0F, 0.0F}) < 0.4F);
    // He went round: off the line, never through the stander's disc.
    CHECK(widest > 0.3F);
    CHECK(nearest > 0.5F);
}

TEST_CASE("a brain's move lasts one update: the next update asks for nothing unless it is written again",
          "[ai][move]") {
    // Brain_UpdateGoals starts with Brain_StopMove (docs/research/characters.md#ai-locomotion).
    MoveScene scene;
    Brain& brain = scene.add({20.0F, 20.0F, 0.0F}, 0.0F);
    brain.setMoveHeading(radians(90.0F), 2.0F);
    REQUIRE(brain.human().record().move.has_value());
    scene.run(1);
    CHECK_FALSE(brain.human().record().move.has_value());
    // An action that writes it each update keeps it.
    const HeldMove held{.heading = radians(90.0F), .speed = 2.0F, .aim = std::nullopt};
    REQUIRE(brain.queueAction(std::make_unique<HoldMoveAction>(held)));
    scene.run(3);
    const auto& kept = brain.human().record().move;
    REQUIRE(kept.has_value());
    if (kept.has_value()) {
        CHECK(kept->speed == 2.0F);
    }
}

namespace {

// Locks `brain`'s human onto `other`'s: an attack on him makes him the fighter's target. Waits for the attack to be
// over; then neither starts a fight of his own, which would push a goal and clear the actions.
void lockOnto(MoveScene& scene, Brain& brain, Brain& other) {
    scene.step.setOpposition([](const Human& /*one*/, const Human& /*other*/) { return true; });
    brain.setTarget(&other);
    REQUIRE(brain.queueAction(std::make_unique<coney::ai::AttackAction>(1, 0)));
    scene.runUntil(
        120,
        [&] {
            return brain.human().fighter().lockTarget() != nullptr &&
                   !coney::human::stickBusy(brain.human().gateInput()) && brain.human().animator().flags() == 0;
        },
        [] {});
    REQUIRE(brain.human().fighter().lockTarget() != nullptr);
    brain.setTarget(nullptr);
    brain.setThreatResponse(0);
    other.setThreatResponse(0);
    // Whatever the hit started in him (a fight back) goes; both settle before the test asks anything.
    other.flush();
    brain.flush();
    const auto settled = [](const Human& human) {
        return !coney::human::stickBusy(human.gateInput()) && human.animator().flags() == 0 &&
               !human.fighter().holdsMovement(human.animator());
    };
    scene.runUntil(120, [&] { return settled(brain.human()) && settled(other.human()); }, [] {});
    REQUIRE(settled(brain.human()));
    REQUIRE(settled(other.human()));
    REQUIRE(brain.human().fighter().lockTarget() != nullptr);
}

// The heading change from `before` to `after`, degrees.
float turnedDegrees(float before, float after) {
    return coney::human::wrapAngle(after - before) * 180.0F / std::numbers::pi_v<float>;
}

// Restores the player's stance turn (a debug tunable) when it goes.
struct StanceTurnScope {
    float saved = coney::human::locomotionTuning().stanceTurnDegrees;
    StanceTurnScope() = default;
    StanceTurnScope(const StanceTurnScope&) = delete;
    StanceTurnScope& operator=(const StanceTurnScope&) = delete;
    StanceTurnScope(StanceTurnScope&&) = delete;
    StanceTurnScope& operator=(StanceTurnScope&&) = delete;
    ~StanceTurnScope() { coney::human::locomotionTuning().stanceTurnDegrees = saved; }
};

} // namespace

TEST_CASE("locked onto a target, a move turns away from it and walks to its point", "[ai][move]") {
    // The stance faces the brain's heading, not the target (docs/research/characters.md#ai-locomotion): a move whose
    // point lies behind the human, its target in front, turns on the spot and strafes there. Facing the target, the
    // turn could never finish and the move would stand for good (the level5 Vargas stall).
    MoveScene scene;
    Brain& brain = scene.add({45.0F, 41.0F, 0.0F}, -90.0F);                               // facing +x
    Brain& other = scene.add({45.9F, 41.0F, 0.0F}, 90.0F, coney::ai::BrainType::Warrior); // in front of him
    lockOnto(scene, brain, other);
    // Behind him, near enough that the target is kept (2.5 m).
    brain.queueAction(moveTo({43.6F, 41.0F, 0.0F}, 0.3F));
    bool lockHeld = true;
    float widest = 0.0F;
    float heading = brain.human().heading();
    const int steps = scene.runUntil(
        300, [&] { return brain.actionCount() == 0; },
        [&] {
            lockHeld = lockHeld && brain.human().fighter().lockTarget() != nullptr;
            widest = std::max(widest, std::fabs(turnedDegrees(heading, brain.human().heading())));
            heading = brain.human().heading();
        });
    CHECK(steps < 300);
    CHECK(lockHeld);
    CHECK(brain.moveFailure() == MoveFailure::None);
    CHECK(planDistance(brain.human().position(), Vec3{43.6F, 41.0F, 0.0F}) <= 0.5F);
    // Never more than the stance's 24° in an update.
    CHECK(widest <= 24.0F + 1e-3F);
}

TEST_CASE("locked onto a target with no move, an AI keeps its facing while the target walks round him", "[ai][move]") {
    // Nothing turns a fighter to his target but a move or an attack (docs/research/ai.md#fight-facing).
    MoveScene scene;
    Brain& brain = scene.add({45.0F, 41.0F, 0.0F}, -90.0F);
    Brain& other = scene.add({45.9F, 41.0F, 0.0F}, 90.0F, coney::ai::BrainType::Warrior);
    lockOnto(scene, brain, other);
    const float facing = brain.human().heading();
    const auto bearing = [&] {
        return coney::human::headingOf(coney::anim::subtract(other.human().position(), brain.human().position()));
    };
    const float bearingBefore = bearing();
    const Vec3 standing = brain.human().position();
    // He walks 1.8 m up +y beside him, within the 2.5 m that keeps the lock. The hit left him in a fight stance too,
    // where the feet go to his brain's aim point (+0x90), not along the heading
    // (docs/research/characters.md#ai-locomotion), so the walk aims there. With the aim left stale he strafed into the
    // AI along x and, now that humans meet (docs/research/physics.md#human-contacts), pushed him along, so the bearing
    // never changed.
    const HeldMove walk{.heading = 0.0F, .speed = other.human().speeds().walk, .aim = Vec3{45.9F, 42.8F, 0.0F}};
    REQUIRE(other.queueAction(std::make_unique<HoldMoveAction>(walk)));
    bool lockHeld = true;
    scene.runUntil(
        30, [] { return false; }, [&] { lockHeld = lockHeld && brain.human().fighter().lockTarget() != nullptr; });
    CHECK(lockHeld);
    CHECK(std::fabs(turnedDegrees(bearingBefore, bearing())) > 30.0F);
    CHECK(brain.human().heading() == facing);
    // Beside him, not through him: nobody pushed the AI.
    CHECK(planDistance(brain.human().position(), standing) < 0.01F);
}

TEST_CASE("locked, a brain's move turns an AI 24 degrees an update and strafes him to its point", "[ai][move]") {
    // The stance (docs/research/characters.md#ai-locomotion): the turn toward the brain's heading runs whenever it asks
    // for a speed, at the AI's own word (0x005101dc), which the player's tuning never changes; the feet go along the
    // line to the point at the stance walk when it asks for more than 0.1, else stand.
    const StanceTurnScope scope;
    coney::human::locomotionTuning().stanceTurnDegrees = 10.0F; // the player's: not the AI's
    MoveScene scene;
    Brain& brain = scene.add({45.0F, 41.0F, 0.0F}, -90.0F);
    Brain& other = scene.add({45.9F, 41.0F, 0.0F}, 90.0F, coney::ai::BrainType::Warrior);
    lockOnto(scene, brain, other);
    const float facing = brain.human().heading();
    const Vec3 at = brain.human().position();
    // Asked to face 90° to his left, his point on him: he turns 24° an update and stays where he is.
    HeldMove held{.heading = facing + radians(90.0F), .speed = brain.human().speeds().walk, .aim = at};
    REQUIRE(brain.queueAction(std::make_unique<HoldMoveAction>(held)));
    std::vector<float> turns;
    for (int k = 0; k < 4; ++k) {
        const float before = brain.human().heading();
        scene.run(1);
        turns.push_back(turnedDegrees(before, brain.human().heading()));
    }
    CHECK(turns[0] == Catch::Approx(24.0F).margin(1e-3));
    CHECK(turns[1] == Catch::Approx(24.0F).margin(1e-3));
    CHECK(turns[2] == Catch::Approx(24.0F).margin(1e-3));
    CHECK(turns[3] == Catch::Approx(18.0F).margin(1e-3));
    CHECK(planDistance(brain.human().position(), at) < 0.05F);
    REQUIRE(brain.human().fighter().lockTarget() != nullptr);
    // Asked for 0.05 m/s: still turned (back toward the target), not moved.
    held.heading = facing;
    held.speed = 0.05F;
    held.aim = Vec3{at.x, at.y + 5.0F, at.z};
    const float before = brain.human().heading();
    scene.run(1);
    CHECK(turnedDegrees(before, brain.human().heading()) == Catch::Approx(-24.0F).margin(1e-3));
    CHECK(planDistance(brain.human().position(), at) < 0.05F);
    // Asked to walk: he strafes up +y to the point while he faces the target, at the stance walk in the end.
    held.speed = brain.human().speeds().walk;
    scene.run(20);
    CHECK(brain.human().heading() == Catch::Approx(facing).margin(1e-4));
    CHECK(brain.human().position().y - at.y > 1.0F);
    CHECK(std::fabs(brain.human().position().x - at.x) < 0.05F);
    CHECK(brain.human().speed() == Catch::Approx(coney::combat::combatTuning().combatWalkSpeed).margin(0.01));
    REQUIRE(brain.human().fighter().lockTarget() != nullptr);
}

TEST_CASE("locked and wounded, an AI turns 0.375 degrees an update, even asked to stand, and walks", "[ai][move]") {
    // The hurt test comes first (docs/research/characters.md#ai-locomotion): a quarter of 1.5° an update whatever the
    // speed, no turn on the spot, and the walk speed rather than the stance walk.
    MoveScene scene;
    Brain& brain = scene.add({45.0F, 41.0F, 0.0F}, -90.0F);
    Brain& other = scene.add({45.9F, 41.0F, 0.0F}, 90.0F, coney::ai::BrainType::Warrior);
    lockOnto(scene, brain, other);
    // **Coney's test**: the mark alone (HuSetWounded would end the stance first).
    brain.human().script().wounded = true;
    const float facing = brain.human().heading();
    const Vec3 at = brain.human().position();
    HeldMove held{.heading = facing + radians(90.0F), .speed = 0.0F, .aim = at};
    REQUIRE(brain.queueAction(std::make_unique<HoldMoveAction>(held)));
    scene.run(1);
    CHECK(turnedDegrees(facing, brain.human().heading()) == Catch::Approx(0.375F).margin(1e-3));
    held.speed = brain.human().speeds().walk;
    held.aim = Vec3{at.x, at.y + 5.0F, at.z};
    const float before = brain.human().heading();
    scene.run(10);
    CHECK(turnedDegrees(before, brain.human().heading()) == Catch::Approx(3.75F).margin(1e-2));
    CHECK(brain.human().speed() == Catch::Approx(brain.human().speeds().walk).margin(0.01));
    CHECK(brain.human().position().y > at.y + 0.3F);
}

TEST_CASE("a fast climber at the run tries the next leg's climb within 4.5 m of its far waypoint",
          "[ai][move][climb]") {
    using coney::ai::earlyClimbDue;
    using coney::human::Gait;
    namespace edge_flag = coney::ai::edge_flag;
    // Route_TryClimbNear (docs/research/ai-moving.md#route-follow): a climb leg next (kind 8 or 0x80), flag 0x2, a gait above
    // the jog, within 4.5 m (3D).
    CHECK(earlyClimbDue(edge_flag::kClimb, true, Gait::Run, 4.5F));
    CHECK(earlyClimbDue(edge_flag::kJump, true, Gait::Sprint, 1.0F));
    CHECK_FALSE(earlyClimbDue(edge_flag::kClimb, true, Gait::Run, 4.6F));
    CHECK_FALSE(earlyClimbDue(edge_flag::kClimb, true, Gait::Jog, 2.0F));
    CHECK_FALSE(earlyClimbDue(edge_flag::kClimb, false, Gait::Run, 2.0F));
    CHECK_FALSE(earlyClimbDue(1, true, Gait::Run, 2.0F));
    CHECK_FALSE(earlyClimbDue(edge_flag::kJumpDown, true, Gait::Run, 2.0F));
}
