// SPDX-License-Identifier: GPL-3.0-or-later
// The gang tactics (docs/research/ai.md#tactics): the base's start, time limit and callback, TacticCrowd and
// TacticTrigger; and GoalDealer (docs/research/ai.md#dealer).
#include "ai/tactic.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/dealer_goal.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/idle_goals.h"
#include "ai/tactic_crowd.h"
#include "human/human.h"
#include "support/ai_fixtures.h"
#include "warriors/inventory.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::test::AiScene;

namespace {

// A gang tactic that does nothing but count its updates.
class CountingTactic final : public coney::ai::Tactic {
  public:
    CountingTactic(std::string callback, std::int64_t limitMs) : Tactic(0x20, std::move(callback), limitMs) {}
    void start(coney::ai::Gang& /*gang*/) override { ++starts; }
    [[nodiscard]] int update(coney::ai::Gang& /*gang*/) override {
        ++updates;
        return 0;
    }
    int starts = 0;
    int updates = 0;
};

// A gang of kind 19 named `name` with `count` members in a row east of the player.
int makeGang(AiScene& scene, const std::string& name, int count, std::vector<Brain*>& members) {
    const int gang = scene.brains.gangs().create(19, name);
    for (int k = 0; k < count; ++k) {
        Brain& member = scene.add({44.0F + 2.0F * static_cast<float>(k), 40.0F, 0.0F}, 0.0F);
        scene.brains.gangs().addMember(gang, member);
        members.push_back(&member);
    }
    return gang;
}

} // namespace

TEST_CASE("a tactic starts by flushing its AI members, and past its time limit fires its callback with 2",
          "[ai][tactics]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeGang(scene, "Crew", 2, members);
    scene.brains.gangs().addMember(gang, scene.player());
    for (Brain* member : members) {
        member->pushGoal(std::make_unique<coney::ai::IdleGoal>());
    }
    scene.player().pushGoal(std::make_unique<coney::ai::IdleGoal>());
    auto owned = std::make_unique<CountingTactic>("P1.Done", 200);
    CountingTactic& tactic = *owned;
    scene.brains.gangs().setTactic(gang, std::move(owned));
    CHECK_FALSE(tactic.keepsOwnGoals());

    scene.run(1);
    CHECK(tactic.started());
    CHECK(tactic.starts == 1);
    CHECK(members[0]->goalCount() == 0);
    CHECK(scene.player().goalCount() == 1); // the player is not flushed
    CHECK(scene.services.calls.empty());

    scene.run(8); // 300 ms: past the limit
    REQUIRE_FALSE(scene.services.calls.empty());
    const coney::test::ScriptCall& call = scene.services.calls.front();
    CHECK(call.function == "P1.Done");
    CHECK(call.args == std::vector<double>{static_cast<double>(gang), 2.0});
}

TEST_CASE("TacticCrowd seats a cheering crowd idle and fightless, and a watching one spectating", "[ai][tactics]") {
    AiScene scene;
    std::vector<Brain*> cheering;
    const int fans = makeGang(scene, "Fans", 2, cheering);
    std::vector<Brain*> watching;
    const int lookers = makeGang(scene, "Lookers", 2, watching);
    scene.brains.gangs().setTactic(fans, std::make_unique<coney::ai::TacticCrowd>("", true, 0));
    scene.brains.gangs().setTactic(lookers, std::make_unique<coney::ai::TacticCrowd>("", false, 0));
    scene.run(1);
    for (const Brain* member : cheering) {
        REQUIRE(member->topGoal() != nullptr);
        CHECK(member->topGoal()->type() == GoalType::Idle);
        CHECK(member->threatResponse() == 0);
    }
    for (const Brain* member : watching) {
        REQUIRE(member->topGoal() != nullptr);
        CHECK(member->topGoal()->type() == GoalType::Spectate);
    }
    // A spectate goal lasts 4-6 s.
    scene.run(6 * 30 + 5);
    for (const Brain* member : watching) {
        CHECK(member->goalCount() == 0);
    }
}

TEST_CASE("TacticTrigger switches a crowd's reactions and makes every free member react at once", "[ai][tactics]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeGang(scene, "Fans", 3, members);
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::TacticCrowd>("", true, 0));
    scene.run(1);
    auto& crowd = dynamic_cast<coney::ai::TacticCrowd&>(*scene.brains.gangs().find(gang)->tactic());
    CHECK_FALSE(crowd.reactionsOn());
    crowd.trigger(*scene.brains.gangs().find(gang), 0, true);
    CHECK(crowd.reactionsOn());

    // What 1, on: each free member queues the reaction 0x8f, then the cheer.
    crowd.trigger(*scene.brains.gangs().find(gang), 1, true);
    for (const Brain* member : members) {
        CHECK(member->actionCount() == 2);
    }
    for (int k = 0; k < 60 && members[0]->actionCount() > 0; ++k) {
        scene.run(1);
    }
    CHECK(members[0]->actionCount() == 0);
    REQUIRE(scene.services.clips.size() >= 2);
    CHECK(scene.services.clips[0] == coney::ai::kCrowdReactAnim);

    // What 1, off: the reaction is 0xb1.
    for (Brain* member : members) {
        member->clearActions();
    }
    scene.services.clips.clear();
    crowd.trigger(*scene.brains.gangs().find(gang), 1, false);
    scene.run(40);
    REQUIRE_FALSE(scene.services.clips.empty());
    CHECK(scene.services.clips[0] == coney::ai::kCrowdReactOffAnim);
}

TEST_CASE("a crowd member warned of an attack fires the crowd's callback with 6", "[ai][tactics]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeGang(scene, "Fans", 1, members);
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::TacticCrowd>("P1.Crowd", true, 0));
    scene.run(1);
    CHECK(coney::ai::deliverEvent(*members[0], coney::ai::BrainEvent{.id = coney::ai::kEventAttackWarning}));
    REQUIRE(scene.services.calls.size() == 1);
    CHECK(scene.services.calls[0].function == "P1.Crowd");
    CHECK(scene.services.calls[0].args == std::vector<double>{static_cast<double>(gang), 6.0});
}

TEST_CASE("a dealer's goods follow his class, else the call's type", "[ai][dealer]") {
    CHECK(coney::ai::dealerTypeFor(428, 1) == 0);
    CHECK(coney::ai::dealerTypeFor(433, 0) == 2);
    CHECK(coney::ai::dealerTypeFor(437, 0) == 1);
    CHECK(coney::ai::dealerTypeFor(100, 2) == 2);
}

TEST_CASE("GoalDealer turns to the player, greets him, deals once he is close, and fights again when it ends",
          "[ai][dealer]") {
    AiScene scene;
    // 1.2 m east of the player, facing +y.
    Brain& dealer = scene.add({41.2F, 40.0F, 0.0F}, 0.0F);
    dealer.pushGoal(std::make_unique<coney::ai::DealerGoal>(scene.services, 0, 10.0F, 50, 0, false));
    scene.run(1);
    CHECK(dealer.threatResponse() == 0);
    const auto* goal = dynamic_cast<const coney::ai::DealerGoal*>(dealer.topGoal());
    REQUIRE(goal != nullptr);
    for (int k = 0; k < 300 && !goal->dealing(); ++k) {
        scene.run(1);
    }
    CHECK(goal->greeted());
    CHECK(goal->dealing());
    CHECK(goal->state() == coney::ai::DealerState::Dealing);
    dealer.flush();
    CHECK(dealer.threatResponse() == 2);
}

TEST_CASE("GoalDealer waits while the player is out of range", "[ai][dealer]") {
    AiScene scene;
    Brain& dealer = scene.add({60.0F, 60.0F, 0.0F}, 0.0F);
    dealer.pushGoal(std::make_unique<coney::ai::DealerGoal>(scene.services, 0, 10.0F, 50, 0, false));
    scene.run(60);
    const auto* goal = dynamic_cast<const coney::ai::DealerGoal*>(dealer.topGoal());
    REQUIRE(goal != nullptr);
    CHECK_FALSE(goal->greeted());
    CHECK_FALSE(goal->dealing());
    CHECK(dealer.actionCount() == 0);
}

TEST_CASE("a dealer's terms follow the table: the flash for $20, weapons for $50, spray paint for $5", "[ai][dealer]") {
    const std::optional<coney::ai::DealTerms> flash = coney::ai::dealTerms(0);
    REQUIRE(flash.has_value());
    CHECK(flash->item == coney::item::kRevive);
    CHECK(flash->price == 20);
    CHECK(flash->amount == 1);
    CHECK(coney::ai::dealTerms(1)->price == 50);
    CHECK(coney::ai::dealTerms(2)->item == coney::item::kSprayPaint);
    CHECK_FALSE(coney::ai::dealTerms(3).has_value());
}

TEST_CASE("a dealing dealer refuses a buyer short of money or at the limit, and sells to one with $20",
          "[ai][dealer]") {
    AiScene scene;
    Brain& dealer = scene.add({41.2F, 40.0F, 0.0F}, 0.0F);
    dealer.pushGoal(std::make_unique<coney::ai::DealerGoal>(scene.services, 0, 10.0F, 50, 0, false));
    auto* goal = dynamic_cast<coney::ai::DealerGoal*>(dealer.topGoal());
    REQUIRE(goal != nullptr);
    if (goal == nullptr) {
        return;
    }
    // Not yet dealing: nothing.
    CHECK(goal->deal(dealer, scene.player(), 100, 0, 3) == coney::ai::DealOutcome::NotDealing);
    for (int k = 0; k < 300 && !goal->offering(); ++k) {
        scene.run(1);
    }
    REQUIRE(goal->offering());
    // $20 and no flash: sold, his money up by the price; the offer stays.
    CHECK(goal->deal(dealer, scene.player(), 20, 0, 3) == coney::ai::DealOutcome::Sold);
    CHECK(goal->sales() == 1);
    CHECK(goal->sold());
    CHECK(dealer.human().script().money == 20);
    CHECK(goal->offering());
    // Three flashes carried (the limit): refused, and the offer is withdrawn.
    CHECK(goal->deal(dealer, scene.player(), 20, 3, 3) == coney::ai::DealOutcome::AtLimit);
    CHECK_FALSE(goal->offering());
}

TEST_CASE("a dealer refuses a buyer with less than the price; a dirty one keeps it and leaves", "[ai][dealer]") {
    AiScene scene;
    Brain& poor = scene.add({41.2F, 40.0F, 0.0F}, 0.0F);
    poor.pushGoal(std::make_unique<coney::ai::DealerGoal>(scene.services, 0, 10.0F, 50, 0, false));
    Brain& dirty = scene.add({38.8F, 40.0F, 0.0F}, 0.0F);
    dirty.pushGoal(std::make_unique<coney::ai::DealerGoal>(scene.services, 0, 10.0F, 50, 100, false));
    auto* first = dynamic_cast<coney::ai::DealerGoal*>(poor.topGoal());
    auto* second = dynamic_cast<coney::ai::DealerGoal*>(dirty.topGoal());
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    if (first == nullptr || second == nullptr) {
        return;
    }
    for (int k = 0; k < 300 && !(first->offering() && second->offering()); ++k) {
        scene.run(1);
    }
    REQUIRE(first->offering());
    REQUIRE(second->offering());
    CHECK(first->deal(poor, scene.player(), 19, 0, 3) == coney::ai::DealOutcome::NoCash);
    CHECK_FALSE(first->offering());
    CHECK(poor.human().script().money == 0);
    CHECK(second->dirty());
    CHECK(second->deal(dirty, scene.player(), 20, 0, 3) == coney::ai::DealOutcome::RippedOff);
    CHECK(second->state() == coney::ai::DealerState::Leaving);
    CHECK(dirty.human().script().money == 20);
    CHECK(second->sales() == 0);
}

TEST_CASE("a dealer says nocash, limit, ripoff, and cash at most every 5 s", "[ai][dealer]") {
    AiScene scene;
    Brain& dealer = scene.add({41.2F, 40.0F, 0.0F}, 0.0F);
    dealer.pushGoal(std::make_unique<coney::ai::DealerGoal>(scene.services, 0, 10.0F, 50, 0, false));
    auto* goal = dynamic_cast<coney::ai::DealerGoal*>(dealer.topGoal());
    REQUIRE(goal != nullptr);
    if (goal == nullptr) {
        return;
    }
    using coney::ai::DealOutcome;
    CHECK(goal->dealLine(DealOutcome::NoCash, 0) == coney::ai::kDealNoCashLine);
    CHECK(goal->dealLine(DealOutcome::AtLimit, 0) == coney::ai::kDealLimitLine);
    CHECK(goal->dealLine(DealOutcome::RippedOff, 0) == coney::ai::kDealRipOffLine);
    CHECK(goal->dealLine(DealOutcome::Sold, 1000) == coney::ai::kDealCashLine);
    CHECK_FALSE(goal->dealLine(DealOutcome::Sold, 5999).has_value());
    CHECK(goal->dealLine(DealOutcome::Sold, 6000) == coney::ai::kDealCashLine);
    CHECK_FALSE(goal->dealLine(DealOutcome::NotDealing, 0).has_value());
}
