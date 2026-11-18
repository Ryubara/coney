// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brain_events.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/sub_tactics.h"
#include "ai/tactic_attack.h"
#include "core/game_random.h"
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"
#include "support/ai_fixtures.h"

// The attack tactic's team-ups (docs/research/ai-reimpl.md#unit-3): the 7 s timer, the gates on the draw (team-ups
// on, a free leader, two living members, an enemy gang in the leader's sight), the weighted draw over the gang kind's
// strategy with its fallback to order 5, the order each draw makes and its member count, a guard order's end and the
// enemy-gang search with the gang bounds it reads. The game's random table is replaced by a synthetic one whose every
// entry is the roll wanted, so a draw is known in advance; the orders are the stand-ins until Units 4 to 6.

using coney::GameRandom;
using coney::RandomStream;
using coney::ai::Brain;
using coney::ai::BrainEvent;
using coney::ai::Gang;
using coney::ai::Gangs;
using coney::ai::GangStrategy;
using coney::ai::GangStrategyTable;
using coney::ai::GoalType;
using coney::ai::OrderArgs;
using coney::ai::OrderKind;
using coney::ai::StandInOrder;
using coney::ai::TacticAttack;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// The gang kinds the scenes use: ours draws, theirs is fought.
constexpr int kOurKind = 19;
constexpr int kTheirKind = 21;

// A random table whose every entry is `roll`: each draw on any stream returns it.
GameRandom tableOf(std::uint32_t roll) {
    GameRandom random;
    std::vector<std::uint32_t> table(GameRandom::kTableSize, roll);
    random.setTable(table);
    return random;
}

// A gang of `kind` named `name` with an AI member at each of `places`; its id.
int makeGang(AiScene& scene, int kind, const std::string& name, const std::vector<Vec3>& places,
             std::vector<Brain*>& members) {
    const int gang = scene.brains.gangs().create(kind, name);
    for (const Vec3& feet : places) {
        Brain& member = scene.add(feet, 0.0F);
        scene.brains.gangs().addMember(gang, member);
        members.push_back(&member);
    }
    return gang;
}

// `count` places in a row along x from (44, 40), a metre apart.
std::vector<Vec3> row(int count, float x = 44.0F, float y = 40.0F) {
    std::vector<Vec3> places;
    for (int i = 0; i < count; ++i) {
        places.push_back({x + static_cast<float>(i), y, 0.0F});
    }
    return places;
}

// A scene of our gang (kind kOurKind, team-ups on, its strategy weighing only draw `draw`) under an attack tactic, and
// an enemy gang that fights; the gangs draw from a table of `roll`s.
struct TeamUpScene {
    GameRandom random; // before the scene, whose gangs draw from it
    AiScene scene;
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    int gang = -1;
    int enemy = -1;

    TeamUpScene(int ourCount, int theirCount, int draw, std::uint32_t roll = 0, Vec3 theirPlace = {50.0F, 40.0F, 0.0F})
        : random(tableOf(roll)) {
        Gangs& gangs = scene.brains.gangs();
        gangs.setGameRandom(&random);
        GangStrategyTable strategies{};
        if (draw >= 0) {
            strategies[kOurKind][static_cast<std::size_t>(draw)] = 1;
        }
        gangs.setStrategies(strategies);
        gang = makeGang(scene, kOurKind, "Riffs", row(ourCount), ours);
        gangs.find(gang)->orders().attackStrategies = true;
        if (theirCount > 0) {
            enemy = makeGang(scene, kTheirKind, "Orphans", row(theirCount, theirPlace.x, theirPlace.y), theirs);
            gangs.makeEnemies(gang, enemy);
            gangs.find(enemy)->setAlertState(coney::ai::kGangFighting);
        }
        coney::ai::tacticAttack(gangs, gang, "");
    }

    Gangs& gangs() { return scene.brains.gangs(); }
    TacticAttack& tactic() { return *dynamic_cast<TacticAttack*>(gangs().find(gang)->tactic()); }

    // The updates up to the draw: the start (which arms the timer for 7016), one update per turn so every gang's bounds
    // are set, one that takes the 1 s enemy check (which ends its update early), then the update past the timer.
    void runToDraw() {
        for (const std::uint64_t now : {16ULL, 100ULL, 200ULL, 300ULL, 400ULL, 7000ULL, 7017ULL}) {
            gangs().update(now);
        }
    }

    // The order the tactic runs; null for none.
    [[nodiscard]] const StandInOrder* order() {
        const std::optional<std::size_t> slot = tactic().strategy();
        return slot ? dynamic_cast<const StandInOrder*>(gangs().subTactics().at(*slot)) : nullptr;
    }
};

// A goal of any type that does nothing, to sit above a member's goal base.
class StubGoal final : public coney::ai::Goal {
  public:
    explicit StubGoal(GoalType type) : Goal(type) {}
    [[nodiscard]] coney::ai::GoalStatus process(Brain& /*brain*/) override { return coney::ai::GoalStatus::Stop; }
};

// The goal type on top of `brain`'s stack; nothing when it is empty.
std::optional<GoalType> top(const Brain& brain) {
    const coney::ai::Goal* goal = brain.topGoal();
    return goal != nullptr ? std::optional<GoalType>(goal->type()) : std::nullopt;
}

} // namespace

TEST_CASE("the team-up draw takes the first order whose running weight exceeds the roll, and order 5 at the total",
          "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    const GangStrategy weights{20, 20, 20, 20, 20, 20, 0};
    for (const auto& [roll, draw] :
         std::array<std::pair<std::uint32_t, int>, 5>{{{0, 0}, {19, 0}, {20, 1}, {119, 5}, {120, 5}}}) {
        GameRandom random = tableOf(roll);
        CHECK(coney::ai::drawTeamUp(weights, random) == draw);
        // One draw, on the tactics stream.
        CHECK(random.index(RandomStream::Tactics) == 1);
        CHECK(random.index(RandomStream::Gameplay) == 0);
    }
    // Random_Int(120) is 0 to 120 inclusive: a roll of 121 wraps to 0.
    GameRandom wrap = tableOf(121);
    CHECK(coney::ai::drawTeamUp(weights, wrap) == 0);
}

TEST_CASE("a strategy whose weights are all 0 always draws order 5", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    for (const std::uint32_t roll : {0U, 1U, 77U}) {
        GameRandom random = tableOf(roll);
        CHECK(coney::ai::drawTeamUp(GangStrategy{}, random) == 5);
    }
}

TEST_CASE("a charge order takes max(L / 2, L - E) members for 10 s", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    TeamUpScene few(4, 1, 0); // L - E = 3 beats L / 2 = 2
    few.runToDraw();
    REQUIRE(few.order() != nullptr);
    CHECK(few.order()->kind() == OrderKind::Charge);
    CHECK(few.order()->args().count == 3);
    CHECK(few.order()->args().limitMs == 10000);
    // Made after this update's strategy update: it starts on the next.
    CHECK_FALSE(few.order()->started());
    few.gangs().update(7100);
    CHECK(few.order()->started());
    CHECK(few.order()->endsAtMs() == 7100 + 10000);

    TeamUpScene many(5, 4, 0); // L / 2 = 2 (halved down) beats L - E = 1
    many.runToDraw();
    REQUIRE(many.order() != nullptr);
    CHECK(many.order()->args().count == 2);
}

TEST_CASE("draws 3 and 5 make a throw order of max(L / 2, L - E) for 20 s, from a pile for 3",
          "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    TeamUpScene pile(4, 3, 3);
    pile.runToDraw();
    REQUIRE(pile.order() != nullptr);
    CHECK(pile.order()->kind() == OrderKind::Throw);
    CHECK(pile.order()->args().count == 2);
    CHECK(pile.order()->args().flag);
    CHECK(pile.order()->args().limitMs == 20000);

    TeamUpScene plain(4, 1, 5);
    plain.runToDraw();
    REQUIRE(plain.order() != nullptr);
    CHECK(plain.order()->kind() == OrderKind::Throw);
    CHECK(plain.order()->args().count == 3);
    CHECK_FALSE(plain.order()->args().flag);
}

TEST_CASE("draw 4 makes a leader order of L - 1 members for 15 s", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    TeamUpScene scene(4, 3, 4);
    scene.runToDraw();
    REQUIRE(scene.order() != nullptr);
    CHECK(scene.order()->kind() == OrderKind::Leader);
    CHECK(scene.order()->args().count == 3);
    CHECK(scene.order()->args().limitMs == 15000);
}

TEST_CASE("draws 1 and 2 make a guard order with a 15 s deadline, backing off for 2; draw 6 a grab with no limit",
          "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    TeamUpScene guard(3, 1, 1);
    guard.runToDraw();
    REQUIRE(guard.order() != nullptr);
    CHECK(guard.order()->kind() == OrderKind::Guard);
    CHECK_FALSE(guard.order()->args().flag);
    CHECK(guard.order()->args().limitMs < 0);
    CHECK(guard.order()->args().deadlineAtMs == 7017 + 15000);

    TeamUpScene backOff(3, 1, 2);
    backOff.runToDraw();
    REQUIRE(backOff.order() != nullptr);
    CHECK(backOff.order()->kind() == OrderKind::Guard);
    CHECK(backOff.order()->args().flag);

    TeamUpScene grab(3, 1, 6);
    grab.runToDraw();
    REQUIRE(grab.order() != nullptr);
    CHECK(grab.order()->kind() == OrderKind::Grab);
    CHECK(grab.order()->args().limitMs < 0);
    // The grab stand-in finds nothing to grab and is done on its first process, the next update.
    grab.gangs().update(7100);
    CHECK(grab.tactic().strategy() == std::nullopt);
    CHECK(grab.gangs().subTactics().inUse() == 0);
}

TEST_CASE("no team-up without +0xd9, with a busy leader or with fewer than two living members",
          "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    TeamUpScene off(3, 1, 0);
    off.gangs().find(off.gang)->orders().attackStrategies = false;
    off.runToDraw();
    CHECK_FALSE(off.tactic().strategy().has_value());
    CHECK(off.random.index(RandomStream::Tactics) == 0);

    TeamUpScene busy(3, 1, 0);
    busy.ours[0]->human().stunFor(0, 100000); // the leader is stunned over the draw
    REQUIRE(busy.gangs().find(busy.gang)->leader() == busy.ours[0]);
    busy.runToDraw();
    CHECK_FALSE(busy.tactic().strategy().has_value());
    CHECK(busy.random.index(RandomStream::Tactics) == 0);

    TeamUpScene alone(1, 1, 0);
    alone.runToDraw();
    CHECK_FALSE(alone.tactic().strategy().has_value());
    CHECK(alone.random.index(RandomStream::Tactics) == 0);
}

TEST_CASE("no team-up with no enemy gang fighting within the leader's sight", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    TeamUpScene far(3, 2, 0, 0, {78.0F, 78.0F, 0.0F}); // over 30 m away
    far.runToDraw();
    CHECK_FALSE(far.tactic().strategy().has_value());

    TeamUpScene calm(3, 2, 0);
    calm.gangs().find(calm.enemy)->setAlertState(coney::ai::kGangCalm);
    calm.runToDraw();
    CHECK_FALSE(calm.tactic().strategy().has_value());

    TeamUpScene none(3, 0, 0);
    none.runToDraw();
    CHECK_FALSE(none.tactic().strategy().has_value());
    CHECK(none.random.index(RandomStream::Tactics) == 0);
}

TEST_CASE("the enemy count comes from the lowest-id qualifying gang, not the nearest", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    TeamUpScene scene(4, 1, 0, 0, {60.0F, 40.0F, 0.0F}); // gang 1: one member, 15 m off
    std::vector<Brain*> nearer;
    const int near = makeGang(scene.scene, kTheirKind, "Rogues", row(3, 49.0F), nearer); // gang 2: three, 5 m off
    scene.gangs().makeEnemies(scene.gang, near);
    scene.gangs().find(near)->setAlertState(coney::ai::kGangFighting);
    scene.runToDraw();
    REQUIRE(scene.order() != nullptr);
    CHECK(scene.order()->args().count == 3); // L - E with E = 1; the nearer gang's E = 3 would give 2
}

TEST_CASE("the team-up timer arms 7 s ahead and re-arms on the update after an attempt that starts nothing",
          "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#attack-process, confirmed (code)
    TeamUpScene scene(3, 0, 0); // no enemy gang: every attempt starts nothing
    Gangs& gangs = scene.gangs();
    gangs.update(16);
    CHECK(scene.tactic().teamUpAtMs() == 16 + 7000);
    gangs.update(7000); // the enemy check ends this update before the timer
    CHECK(scene.tactic().teamUpAtMs() == 16 + 7000);
    gangs.update(7016); // not past it yet
    CHECK(scene.tactic().teamUpAtMs() == 16 + 7000);
    gangs.update(7017);
    CHECK(scene.tactic().teamUpAtMs() == 0);
    gangs.update(7100);
    CHECK(scene.tactic().teamUpAtMs() == 7100 + 7000);
}

TEST_CASE("while an order runs the timer stays off and a hit gives no Melee", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#attack-process, confirmed (code)
    TeamUpScene scene(3, 1, 0);
    scene.runToDraw();
    REQUIRE(scene.order() != nullptr);
    scene.gangs().update(7100);
    CHECK(scene.tactic().teamUpAtMs() == 0);
    Gang& riffs = *scene.gangs().find(scene.gang);
    Brain& member = *scene.ours[1];
    member.clearGoals();
    static_cast<void>(riffs.tactic()->event(riffs, member, BrainEvent{.id = coney::ai::kEventDamaged}));
    CHECK(member.goalCount() == 0);
}

TEST_CASE("a guard order's end gives the members the Melee again; a charge's end does not", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#the-strategy-slot-0x00306b50-0x00306b88-0x00306bd0, confirmed (code)
    TeamUpScene guard(3, 1, 1);
    guard.runToDraw();
    REQUIRE(guard.order() != nullptr);
    guard.ours[1]->pushGoal(std::make_unique<StubGoal>(GoalType::Fight));
    guard.gangs().update(7017 + 15001); // past the guard's deadline
    CHECK_FALSE(guard.tactic().strategy().has_value());
    CHECK(top(*guard.ours[1]) == GoalType::Melee);

    TeamUpScene charge(3, 1, 0);
    charge.runToDraw();
    REQUIRE(charge.order() != nullptr);
    charge.gangs().update(7100); // the charge starts
    charge.ours[1]->pushGoal(std::make_unique<StubGoal>(GoalType::Fight));
    charge.gangs().update(7100 + 10001); // past the charge's limit
    CHECK_FALSE(charge.tactic().strategy().has_value());
    CHECK(top(*charge.ours[1]) == GoalType::Fight);
}

TEST_CASE("a gang's bounds: the box round its members not cuffed or knocked out, the radius its larger x or y half",
          "[ai][gangs][teamup]") {
    // Expected: ai-gangs.md#gang-record, confirmed (code)
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> members;
    const int id =
        makeGang(scene, kOurKind, "Riffs",
                 {{40.0F, 30.0F, 0.0F}, {44.0F, 32.0F, 0.0F}, {41.0F, 31.0F, 0.0F}, {70.0F, 70.0F, 0.0F}}, members);
    members[3]->human().setArrested(true); // cuffed: left out
    Gang& gang = *gangs.find(id);
    gang.updateBounds();
    CHECK(gang.centre().x == 42.0F);
    CHECK(gang.centre().y == 31.0F);
    CHECK(gang.radius() == 2.0F);

    // With no member counted: the empty box's centre and radius.
    for (Brain* member : members) {
        member->human().setArrested(true);
    }
    gang.updateBounds();
    CHECK(gang.centre().x == 0.0F);
    CHECK(gang.centre().y == 0.0F);
    CHECK(gang.radius() == -1.0e9F);
}

TEST_CASE("the gangs update their bounds one fifth at a time, gangs 0 and 5 first", "[ai][gangs][teamup]") {
    // Expected: ai-gangs.md#gang-update, confirmed (code)
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<std::vector<Brain*>> members(7);
    for (int i = 0; i < 7; ++i) {
        REQUIRE(makeGang(scene, kOurKind, "Gang" + std::to_string(i), {{50.0F, 50.0F, 0.0F}}, members[i]) == i);
    }
    gangs.update(16);
    CHECK(gangs.find(0)->centre().x == 50.0F);
    CHECK(gangs.find(5)->centre().x == 50.0F);
    CHECK(gangs.find(1)->centre().x == 0.0F);
    CHECK(gangs.find(6)->centre().x == 0.0F);
    gangs.update(32);
    CHECK(gangs.find(1)->centre().x == 50.0F);
    CHECK(gangs.find(6)->centre().x == 50.0F);
    CHECK(gangs.find(2)->centre().x == 0.0F);
}

TEST_CASE("the gang search takes the first gang by id within the radius whose own radius is no larger",
          "[ai][gangs][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> a;
    std::vector<Brain*> b;
    std::vector<Brain*> c;
    std::vector<Brain*> d;
    const int self = makeGang(scene, kOurKind, "Riffs", {{40.0F, 40.0F, 0.0F}}, a);
    const int wide = makeGang(scene, kTheirKind, "Wide", {{45.0F, 40.0F, 0.0F}, {57.0F, 40.0F, 0.0F}}, b); // radius 6
    const int far = makeGang(scene, kTheirKind, "Far", {{75.0F, 40.0F, 0.0F}}, c);
    const int fit = makeGang(scene, kTheirKind, "Fit", {{60.0F, 40.0F, 0.0F}}, d);
    for (const int id : {self, wide, far, fit}) {
        gangs.find(id)->updateBounds();
    }
    const Gang& from = *gangs.find(self);
    CHECK(gangs.findWithinRadius(30.0F, from, nullptr) == gangs.find(wide));
    CHECK(gangs.findWithinRadius(5.0F, from, nullptr) == nullptr); // wide's radius 6 is larger; the others too far
    CHECK(gangs.findWithinRadius(20.5F, from, [](const Gang& /*self*/, const Gang& other) {
        return other.name() == "Fit";
    }) == gangs.find(fit));
    CHECK(gangs.findWithinRadius(
              19.5F, from, [](const Gang& /*self*/, const Gang& other) { return other.name() == "Fit"; }) == nullptr);
}

TEST_CASE("a gang may join a fight against an enemy gang that fights and has a member free to help",
          "[ai][gangs][teamup]") {
    // Expected: ai-reimpl.md#start-sub-tactic, confirmed (code)
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int self = makeGang(scene, kOurKind, "Riffs", {{40.0F, 40.0F, 0.0F}}, ours);
    const int other = makeGang(scene, kTheirKind, "Orphans", {{45.0F, 40.0F, 0.0F}, {46.0F, 40.0F, 0.0F}}, theirs);
    const Gang& riffs = *gangs.find(self);
    Gang& orphans = *gangs.find(other);
    orphans.setAlertState(coney::ai::kGangFighting);
    CHECK_FALSE(coney::ai::canJoinFightAgainst(riffs, orphans)); // not enemies
    gangs.makeEnemies(self, other);
    CHECK(coney::ai::canJoinFightAgainst(riffs, orphans));
    orphans.setAlertState(coney::ai::kGangCalm);
    CHECK_FALSE(coney::ai::canJoinFightAgainst(riffs, orphans)); // calm, with no pursue tactic
    orphans.setAlertState(coney::ai::kGangFighting);
    theirs[0]->human().setArrested(true);
    CHECK(coney::ai::canJoinFightAgainst(riffs, orphans)); // one still free
    theirs[1]->human().setWounded(true);
    CHECK(orphans.noneAbleToHelp(nullptr));
    CHECK_FALSE(coney::ai::canJoinFightAgainst(riffs, orphans)); // none free to help
    theirs[1]->human().setWounded(false);
    CHECK(orphans.noneAbleToHelp(theirs[1]));
    CHECK_FALSE(orphans.noneAbleToHelp(nullptr));
}

TEST_CASE("CfgGang's strategy table gives a gang kind's seven weights as bytes", "[ai][gangs][teamup]") {
    // Expected: ai-reimpl.md#gang-strategy, confirmed
    // (code)
    namespace script = coney::script;
    script::RecordedCalls recorded;
    // CfgGang(kind, v2, v3, v4, v5, strategy, v7, v8, v9), with the strategy {10, 20, 30, 40, 50, 60, 300}.
    const auto call = [&recorded](double kind, std::vector<double> weights) {
        auto table = std::make_shared<script::Table>();
        for (std::size_t i = 0; i < weights.size(); ++i) {
            REQUIRE(table->set(script::Value(static_cast<double>(i + 1)), script::Value(weights[i])).has_value());
        }
        const std::array<script::Value, 9> args{script::Value(kind), script::Value(2.0), script::Value(1.0),
                                                script::Value(0.0),  script::Value(0.0), script::Value(table),
                                                script::Value(0.0),  script::Value(0.0), script::Value(0.0)};
        recorded.add("CfgGang", args);
    };
    call(4, {1, 1, 1, 1, 1, 1, 1});
    call(4, {10, 20, 30, 40, 50, 60, 300}); // the later call wins; 300 keeps its low byte, 44
    call(7, {5, 6});                        // missing entries are 0
    call(99, {9, 9, 9, 9, 9, 9, 9});        // no such kind: skipped
    const GangStrategyTable strategies = coney::ai::gangStrategiesFrom(recorded);
    CHECK(strategies[4] == GangStrategy{10, 20, 30, 40, 50, 60, 44});
    CHECK(strategies[7] == GangStrategy{5, 6, 0, 0, 0, 0, 0});
    CHECK(strategies[0] == GangStrategy{});

    Gangs gangs;
    gangs.setStrategies(strategies);
    CHECK(gangs.strategyWeight(4, 2) == 30);
    CHECK(gangs.strategyWeight(4, 6) == 44);
    CHECK(gangs.strategyWeight(99, 0) == 0);
    CHECK(gangs.strategyWeight(4, 7) == 0);
}
