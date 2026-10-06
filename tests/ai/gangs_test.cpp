// SPDX-License-Identifier: GPL-3.0-or-later
// The gangs (docs/research/ai.md#gangs): their records, enemies and friends, the brains' switches for a whole gang, and
// the events a member's gang hears before its brain, among them event 18 with the count still standing.
#include "ai/gangs.h"

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/idle_goals.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::Gangs;
using coney::test::AiScene;

TEST_CASE("a gang takes the first free slot under a name not in use", "[ai][gangs]") {
    Gangs gangs;
    CHECK(gangs.create(0, "Warriors") == 0);
    CHECK(gangs.create(19, "Enemy") == 1);
    CHECK(gangs.create(19, "Enemy") == -1);
    gangs.remove(0);
    CHECK(gangs.find(0) == nullptr);
    CHECK(gangs.find(-1) == nullptr);
    CHECK(gangs.create(23, "Bums") == 0);
    for (int k = 2; k < static_cast<int>(coney::ai::kGangSlots); ++k) {
        REQUIRE(gangs.create(19, "Gang" + std::to_string(k)) == k);
    }
    CHECK(gangs.create(19, "OneTooMany") == -1);
}

TEST_CASE("friends are the same gang or kind, both police-like, or made friends; enemies both ways", "[ai][gangs]") {
    Gangs gangs;
    const int warriors = gangs.create(coney::ai::kWarriorsKind, "Warriors");
    const int enemy = gangs.create(19, "Enemy");
    const int teacher = gangs.create(19, "Teacher");
    const int police = gangs.create(coney::ai::kPoliceKind, "Police");
    const int bums = gangs.create(coney::ai::kPoliceLikeKind, "Bums");
    const auto* w = gangs.find(warriors);
    const auto* e = gangs.find(enemy);
    CHECK(Gangs::friends(w, w));
    CHECK(Gangs::friends(e, gangs.find(teacher)));
    CHECK(Gangs::friends(gangs.find(police), gangs.find(bums)));
    CHECK_FALSE(Gangs::friends(w, e));
    CHECK_FALSE(Gangs::friends(w, nullptr));

    gangs.makeFriends(warriors, enemy);
    CHECK(Gangs::friends(w, e));
    CHECK(Gangs::friends(e, w));
    gangs.makeEnemies(warriors, enemy);
    CHECK_FALSE(Gangs::friends(w, e));
    CHECK(Gangs::enemies(w, e));
    CHECK(Gangs::enemies(e, w));
    CHECK(w->friendMask() == 0);
    gangs.makeEnemies(warriors, -1); // ignored
    CHECK(w->enemyMask() == 1U << static_cast<unsigned>(enemy));
}

TEST_CASE("joining another gang flushes the brain; a full gang drops its first member", "[ai][gangs]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int a = gangs.create(19, "A");
    const int b = gangs.create(5, "B");
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(a, brain);
    brain.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    gangs.addMember(a, brain); // already in it: nothing
    CHECK(brain.goalCount() == 1);
    gangs.addMember(b, brain);
    CHECK(brain.goalCount() == 0);
    CHECK(brain.gang() == gangs.find(b));
    CHECK(gangs.find(a)->members().empty());

    std::vector<Brain*> joined;
    for (int k = 0; k < static_cast<int>(coney::ai::kGangMembers); ++k) {
        Brain& member = scene.add({30.0F + static_cast<float>(k), 30.0F, 0.0F}, 0.0F);
        gangs.addMember(a, member);
        joined.push_back(&member);
    }
    CHECK(gangs.find(a)->members().size() == coney::ai::kGangMembers);
    gangs.addMember(a, brain);
    CHECK(gangs.find(a)->members().size() == coney::ai::kGangMembers);
    CHECK(joined.front()->gang() == nullptr);
    CHECK(gangs.find(a)->members().back() == &brain);
}

TEST_CASE("GangSetThreatResponse, GangBrFlush and GangBrDead reach the current members only", "[ai][gangs]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int warriors = gangs.create(coney::ai::kWarriorsKind, "Warriors");
    Brain& member = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& later = scene.add({46.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(warriors, member);
    gangs.addMember(warriors, scene.player());
    later.setThreatResponse(1);
    gangs.setThreatResponse(warriors, 2);
    gangs.addMember(warriors, later);
    CHECK(member.threatResponse() == 2);
    CHECK(later.threatResponse() == 1);

    member.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    gangs.flush(warriors);
    CHECK(member.goalCount() == 0);

    // The Warriors set dead: the player's brain gives up the pad.
    std::vector<bool> pad;
    scene.player().setPadControl([&pad](bool on) { pad.push_back(on); });
    gangs.setDead(warriors, true);
    CHECK(scene.player().dead());
    CHECK(member.dead());
    CHECK(pad == std::vector<bool>{false});
}

TEST_CASE("a suspended gang's members keep still", "[ai][gangs]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(19, "Enemy");
    Brain& member = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(gang, member);
    gangs.suspend(gang, true);
    member.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    scene.run(5);
    CHECK_FALSE(member.topGoal()->started());
    gangs.suspend(gang, false);
    scene.run(5);
    CHECK(member.topGoal()->started());
}

TEST_CASE("event 18 calls the gang's handler once with the member, no attacker and the count still standing",
          "[ai][gangs]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(19, "CombatEnemy2");
    Brain& first = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& second = scene.add({46.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(gang, first);
    gangs.addMember(gang, second);
    gangs.setMessageHandler(gang, coney::ai::kGangMessageDown, "P1.BumDied");
    scene.run(3);
    CHECK(scene.services.calls.empty());

    static_cast<void>(first.human().fighter().health().apply(100000));
    scene.run(3);
    REQUIRE(scene.services.calls.size() == 1);
    const coney::test::ScriptCall& call = scene.services.calls[0];
    CHECK(call.function == "P1.BumDied");
    CHECK_FALSE(call.scheduled);
    CHECK(call.args == std::vector<double>{first.handle(), 0.0, 1.0});

    // A cleared handler is not called.
    gangs.setMessageHandler(gang, coney::ai::kGangMessageDown, "");
    static_cast<void>(second.human().fighter().health().apply(100000));
    scene.run(3);
    CHECK(scene.services.calls.size() == 1);
}

TEST_CASE("a member's gang hears its events before its brain, and a handler returning true uses the event",
          "[ai][gangs]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(19, "Enemy");
    Brain& member = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(gang, member);
    gangs.setMessageHandler(gang, coney::ai::kEventAttackWarning, "P1.Warned");
    const coney::ai::BrainEvent warning{.id = coney::ai::kEventAttackWarning, .other = &scene.player(), .value = 4};

    CHECK(coney::ai::deliverEvent(member, warning));
    CHECK(member.attackWarnings() == 1); // the handler returned nil: the brain has it too
    REQUIRE(scene.services.calls.size() == 1);
    CHECK(scene.services.calls[0].args == std::vector<double>{member.handle(), scene.player().handle(), 4.0});

    scene.services.callResult = true;
    CHECK(coney::ai::deliverEvent(member, warning));
    CHECK(member.attackWarnings() == 1); // used by the handler
    CHECK(scene.services.calls.size() == 2);
}

TEST_CASE("a human's own handlers hear its event before its gang, and one that takes it stops the gang and the brain",
          "[ai][gangs]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(19, "Enemy");
    Brain& member = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(gang, member);
    gangs.setMessageHandler(gang, coney::ai::kEventAttackWarning, "P1.Warned");
    member.setServices(&scene.services);
    const coney::ai::BrainEvent warning{.id = coney::ai::kEventAttackWarning, .other = &scene.player(), .value = 4};

    CHECK(coney::ai::deliverEvent(member, warning));
    REQUIRE(scene.services.humanEvents.size() == 1);
    CHECK(scene.services.humanEvents[0].first == &member);
    CHECK(scene.services.humanEvents[0].second.other == &scene.player());
    CHECK(scene.services.calls.size() == 1); // the gang's handler still ran

    scene.services.humanEventTaken = true;
    CHECK(coney::ai::deliverEvent(member, warning));
    CHECK(scene.services.humanEvents.size() == 2);
    CHECK(scene.services.calls.size() == 1); // not the gang's
    CHECK(member.attackWarnings() == 1);     // nor the brain's
}

TEST_CASE("a fall in a human's health is event 1 about the nearest human that can fight, with the damage",
          "[ai][gangs]") {
    AiScene scene;
    Brain& victim = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& near = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    scene.add({50.0F, 40.0F, 0.0F}, 0.0F);
    victim.setServices(&scene.services);
    scene.run(3);
    CHECK(scene.services.humanEvents.empty());

    static_cast<void>(victim.human().fighter().health().apply(20));
    scene.run(1);
    REQUIRE(scene.services.humanEvents.size() == 1);
    const coney::ai::BrainEvent& event = scene.services.humanEvents[0].second;
    CHECK(event.id == coney::ai::kEventDamaged);
    CHECK(event.other == &near);
    CHECK(event.value == 20);
    scene.run(3);
    CHECK(scene.services.humanEvents.size() == 1); // once per fall
}

TEST_CASE("GangMakeNeutralOfType ends the enmity with every unfriendly gang of a kind, both ways", "[ai][gangs]") {
    Gangs gangs;
    const int warriors = gangs.create(coney::ai::kWarriorsKind, "Warriors");
    const int punks = gangs.create(19, "Punks");
    const int others = gangs.create(21, "Others");
    gangs.makeEnemies(warriors, punks);
    gangs.makeEnemies(warriors, others);
    gangs.makeNeutralOfType(warriors, 19);
    CHECK_FALSE(Gangs::enemies(gangs.find(warriors), gangs.find(punks)));
    CHECK_FALSE(Gangs::enemies(gangs.find(punks), gangs.find(warriors)));
    CHECK(Gangs::enemies(gangs.find(warriors), gangs.find(others)));
    gangs.makeNeutralOfType(-1, 21); // no such gang: ignored
    CHECK(Gangs::enemies(gangs.find(warriors), gangs.find(others)));
}
