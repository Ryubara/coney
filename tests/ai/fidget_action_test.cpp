// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fidget_action.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "support/ai_fixtures.h"

// The fidget action and the fight goal's reposition taunt that queues it: the line, the clip from the gang's combat
// fidgets or the character's own, who never fidgets, and which waiting attacker taunts.
// Research: docs/research/ai-reimpl.md#fidget-action, docs/research/ai.md#fight-reposition

using coney::ai::ActionStatus;
using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::FidgetAction;
using coney::test::AiScene;

namespace {

// The times `brain` said speech command `line`.
int saidBy(const AiScene& scene, const Brain& brain, std::uint32_t line) {
    return static_cast<int>(std::ranges::count_if(
        scene.services.said, [&](const auto& said) { return said.first == &brain && said.second == line; }));
}

} // namespace

TEST_CASE("the combat fidget group's four clips fill its first four of eight slots", "[ai]") {
    CHECK(coney::ai::combatFidgetClip(0) == "combat_fidget_call_front");
    CHECK(coney::ai::combatFidgetClip(3) == "combat_fidget_shuffle");
    CHECK(coney::ai::combatFidgetClip(4).empty());
    CHECK(coney::ai::combatFidgetClip(-1).empty());
}

TEST_CASE("a fight fidget says its line and plays a gang clip or the character's own", "[ai]") {
    AiScene scene;
    Brain& soldier = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    soldier.setServices(&scene.services);
    const int gang = scene.brains.gangs().create(19, "Furies");
    scene.brains.gangs().addMember(gang, soldier);

    // Calm: the group is empty, so every fidget is the character's own 0x25b.
    for (int i = 0; i < 20; ++i) {
        FidgetAction fidget(coney::ai::kFightFidgetAnim, false, 0x11);
        CHECK(fidget.start(soldier) == ActionStatus::Running);
    }
    CHECK(saidBy(scene, soldier, 0x11) == 20);
    CHECK(scene.services.namedClips.empty());
    CHECK(std::ranges::count(scene.services.clips, coney::ai::kFightFidgetAnim) == 20);

    // Fighting: a slot 0-3 of the nine draws gives a combat fidget, the others the character's own.
    scene.brains.gangs().find(gang)->setAlertState(coney::ai::kGangFighting);
    scene.services.clips.clear();
    std::set<std::string> named;
    for (int i = 0; i < 90; ++i) {
        FidgetAction fidget(coney::ai::kFightFidgetAnim, false, 0x11);
        CHECK(fidget.start(soldier) == ActionStatus::Running);
    }
    for (const auto& [human, anim, clip] : scene.services.namedClips) {
        CHECK(human == &soldier);
        CHECK(anim == coney::ai::kFightFidgetAnim);
        named.insert(clip);
    }
    CHECK_FALSE(named.empty());
    CHECK(named.size() <= 4);
    CHECK_FALSE(scene.services.clips.empty());
    CHECK(scene.services.namedClips.size() + scene.services.clips.size() == 90);
}

TEST_CASE("a cop, a dealer and a man not free to fidget do not fidget", "[ai]") {
    AiScene scene;
    Brain& cop = scene.add({44.0F, 40.0F, 0.0F}, 0.0F, BrainType::Cop);
    Brain& dealer = scene.add({46.0F, 40.0F, 0.0F}, 0.0F, BrainType::Dealer);
    Brain& armed = scene.add({48.0F, 40.0F, 0.0F}, 0.0F);
    Brain& knife = scene.add({50.0F, 40.0F, 0.0F}, 0.0F);
    for (Brain* brain : {&cop, &dealer, &armed, &knife}) {
        brain->setServices(&scene.services);
    }
    armed.human().fighter().setAnimSet(3);
    knife.human().fighter().setAnimSet(coney::ai::kKnifeAnimSet);

    FidgetAction copFidget(coney::ai::kFightFidgetAnim, false, 0x11);
    CHECK(copFidget.start(cop) == ActionStatus::Done);
    CHECK(saidBy(scene, cop, 0x11) == 0);
    // The dealer says the line but plays no clip.
    FidgetAction dealerFidget(coney::ai::kFightFidgetAnim, false, 0x11);
    CHECK(dealerFidget.start(dealer) == ActionStatus::Done);
    CHECK(saidBy(scene, dealer, 0x11) == 1);
    CHECK(scene.services.clips.empty());
    // A bat in hand: not free.
    FidgetAction armedFidget(coney::ai::kFightFidgetAnim, false, 0x11);
    CHECK(armedFidget.start(armed) == ActionStatus::Done);
    CHECK(saidBy(scene, armed, 0x11) == 0);
    // A knife in hand: its own fidget, 0x25a.
    FidgetAction knifeFidget(coney::ai::kFightFidgetAnim, false, 0x11);
    CHECK(knifeFidget.start(knife) == ActionStatus::Running);
    CHECK(scene.services.clips == std::vector<int>{coney::ai::kKnifeFidgetAnim});
}

TEST_CASE("a waiting attacker taunts every 1-2 s, the one his man is fighting never does", "[ai]") {
    AiScene scene;
    Brain& player = scene.player();
    // Neither has a kind to attack with, so both always wait (Brain_CheckAttack's "no kind").
    coney::ai::FightSettings unarmed;
    unarmed.attackWeights = coney::ai::AttackWeights{};
    Brain& front = scene.add({40.0F, 40.9F, 0.0F}, 180.0F, BrainType::Gang, unarmed);
    Brain& back = scene.add({40.0F, 35.6F, 0.0F}, 0.0F, BrainType::Gang, unarmed);
    for (Brain* brain : {&front, &back}) {
        brain->setServices(&scene.services);
        brain->setTarget(&player);
        REQUIRE(brain->pushGoal(std::make_unique<coney::ai::FightGoal>()));
    }
    // The first in the player's slot list is the one he fights (he has no target): close in, no taunt.
    REQUIRE(player.attackSlots().front() == &front);
    scene.run(300); // 10 s
    CHECK(saidBy(scene, front, 0x11) == 0);
    // The other waits 4.4 m off: a taunt each 1-2 s after the first at 0.75-1 s, so 5 to 10 in 10 s.
    const int taunts = saidBy(scene, back, 0x11);
    CHECK(taunts >= 5);
    CHECK(taunts <= 10);
}

TEST_CASE("without a gesture slot a waiting attacker does not taunt", "[ai]") {
    AiScene scene;
    scene.services.gesturesAllowed = false;
    Brain& player = scene.player();
    Brain& front = scene.add({40.0F, 40.9F, 0.0F}, 180.0F);
    Brain& back = scene.add({40.0F, 35.6F, 0.0F}, 0.0F);
    for (Brain* brain : {&front, &back}) {
        brain->setServices(&scene.services);
        brain->setTarget(&player);
        REQUIRE(brain->pushGoal(std::make_unique<coney::ai::FightGoal>()));
    }
    scene.run(300);
    CHECK(saidBy(scene, back, 0x11) == 0);
    CHECK(scene.services.gesturesAsked > 0);
}
