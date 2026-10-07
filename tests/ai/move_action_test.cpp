// SPDX-License-Identifier: GPL-3.0-or-later
// The move action over path data (docs/research/ai.md#move-action), the turn actions and ActLookAt
// (docs/research/ai.md#look-at), GoalMoveToFlag (docs/research/ai.md#move-to-flag) and the scripts' hold on brains.
// Synthetic clips, a flat floor and synthetic path data; game time is the steps run (1/30 s each).
#include "ai/move_action.h"

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

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/move_to_flag_goal.h"
#include "ai/pedestrian_goal.h"
#include "ai/play_dyn_animation_goal.h"
#include "ai/route_planner.h"
#include "ai/scripted_brains.h"
#include "ai/turn_action.h"
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

    // A human with its feet at `feet` facing `headingDegrees` (0 faces +y, 90 faces -x), with a gang brain.
    Brain& add(Vec3 feet, float headingDegrees) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        made.setFighterProfile(
            coney::human::FighterProfile{.player = false, .powerClass = coney::ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), feet, headingDegrees);
        step.add(made, false);
        return brains.add(made, coney::ai::BrainType::Gang, coney::ai::FightSettings{},
                          static_cast<std::uint32_t>(humans.size()));
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
    // at the run's 4° an update, swinging out about 3 m (docs/research/ai.md#move-action).
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
    scene.runUntil(60, [] { return false; }, [&] { brain.setMoveHeading(0.0F, run); });
    REQUIRE(brain.human().gait() == coney::human::Gait::Run);
    const auto turnSteps = [&](int boost) {
        brain.setTurnBoost(boost);
        const float before = brain.human().heading();
        brain.setMoveHeading(radians(90.0F), run);
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
