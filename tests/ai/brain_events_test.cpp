// SPDX-License-Identifier: GPL-3.0-or-later
// The brains' event answers (docs/research/ai-brain-types.md#help-calls,
// docs/research/ai-brain-types.md): a help call reaches the humans within its range and their hearing; a
// gang soldier who hears it takes his friend's side, fights the aggressor and walks over (HelpRespond); a soldier hit
// by a stranger fights him. Synthetic scenes.
#include "ai/brain_events.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/enemy_scan.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "core/crowd_limits.h"
#include "core/engine_limits.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// A gang of kind `kind` named `name` with one member at `feet`.
Brain& member(AiScene& scene, int kind, const std::string& name, Vec3 feet) {
    const int gang = scene.brains.gangs().create(kind, name);
    Brain& brain = scene.add(feet, 0.0F);
    scene.brains.gangs().addMember(gang, brain);
    return brain;
}

} // namespace

TEST_CASE("a soldier who hears a help call takes his friend's side, fights the aggressor and walks over",
          "[ai][events]") {
    AiScene scene;
    Brain& victim = member(scene, 19, "Punks", {44.0F, 40.0F, 0.0F});
    Brain& friendOfHis = scene.add({52.0F, 40.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(victim.gang()->id(), friendOfHis);
    Brain& aggressor = member(scene, 21, "Others", {45.0F, 40.0F, 0.0F});
    scene.run(1);

    coney::ai::broadcastHelpCall(victim, coney::ai::kHelpCallShared, &aggressor);
    CHECK(friendOfHis.target() == &aggressor);
    REQUIRE(friendOfHis.topGoal() != nullptr);
    CHECK(friendOfHis.topGoal()->type() == GoalType::HelpRespond);
    CHECK(friendOfHis.findGoal(GoalType::Fight) != nullptr);
    // The aggressor hears it too, but sides with no one against himself.
    CHECK(aggressor.findGoal(GoalType::HelpRespond) == nullptr);

    // Far from the target he walks over; once his enemies are listed the fight takes over within 30 updates.
    scene.run(31);
    CHECK(friendOfHis.findGoal(GoalType::HelpRespond) == nullptr);
    CHECK(friendOfHis.findGoal(GoalType::Melee) != nullptr);
    CHECK(friendOfHis.target() == &aggressor);
}

TEST_CASE("a help call reaches no one beyond its range or the hearer's help hearing", "[ai][events]") {
    AiScene scene;
    Brain& victim = member(scene, 19, "Punks", {44.0F, 40.0F, 0.0F});
    Brain& deaf = scene.add({52.0F, 40.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(victim.gang()->id(), deaf);
    deaf.senses().helpHearRange = 5.0F; // 8 m off
    Brain& far = scene.add({44.0F, 66.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(victim.gang()->id(), far); // 26 m off, beyond 20
    Brain& aggressor = member(scene, 21, "Others", {45.0F, 40.0F, 0.0F});
    scene.run(1);

    coney::ai::broadcastHelpCall(victim, coney::ai::kHelpCallShared, &aggressor);
    CHECK(deaf.target() == nullptr);
    CHECK(far.target() == nullptr);
}

TEST_CASE("a gang soldier hit by a stranger fights him and calls for help", "[ai][events]") {
    AiScene scene;
    Brain& soldier = member(scene, 19, "Punks", {44.0F, 40.0F, 0.0F});
    Brain& mate = scene.add({50.0F, 40.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(soldier.gang()->id(), mate);
    Brain& hitter = member(scene, 21, "Others", {45.0F, 40.0F, 0.0F});
    scene.run(1);

    static_cast<void>(coney::ai::deliverEvent(
        soldier, coney::ai::BrainEvent{.id = coney::ai::kEventDamaged, .other = &hitter, .value = 10}));
    CHECK(soldier.target() == &hitter);
    CHECK(soldier.findGoal(GoalType::Fight) != nullptr);
    // The gangs are enemies now.
    CHECK(coney::ai::Gangs::enemies(soldier.gang(), hitter.gang()));
}

TEST_CASE("a friend's hit starts no fight", "[ai][events]") {
    AiScene scene;
    Brain& soldier = member(scene, 19, "Punks", {44.0F, 40.0F, 0.0F});
    Brain& mate = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(soldier.gang()->id(), mate);
    scene.run(1);

    static_cast<void>(coney::ai::deliverEvent(
        soldier, coney::ai::BrainEvent{.id = coney::ai::kEventDamaged, .other = &mate, .value = 10}));
    CHECK(soldier.target() == nullptr);
    CHECK(soldier.findGoal(GoalType::Fight) == nullptr);
}

TEST_CASE("a knocked-out attacker lets go of his target's slot and place and his brain stops until he is up",
          "[ai][events]") {
    AiScene scene;
    Brain& target = member(scene, 19, "Punks", {44.0F, 40.0F, 0.0F});
    Brain& attacker = member(scene, 21, "Others", {45.0F, 40.0F, 0.0F});
    REQUIRE(attacker.fight(target, coney::ai::kNoFightLimit));
    scene.run(1);
    REQUIRE(std::ranges::find(target.attackSlots(), &attacker) != target.attackSlots().end());
    REQUIRE(attacker.goalCount() > 0);

    attacker.human().fighter().health().set(0);
    scene.run(1);
    CHECK(attacker.knockedOut());
    // His knock-out ends a player's slow motion (`Human_KnockOut`, docs/research/slow-motion.md#off), once.
    CHECK(attacker.human().takeSlowMotionEnd());
    CHECK_FALSE(attacker.human().takeSlowMotionEnd());
    CHECK(attacker.target() == nullptr);
    CHECK(attacker.goalCount() == 0);
    CHECK(attacker.enemies().empty());
    CHECK(std::ranges::find(target.attackSlots(), &attacker) == target.attackSlots().end());
    CHECK_FALSE(target.fightBook().places.holds(&attacker));

    // Up again, the brain runs again.
    attacker.human().fighter().health().set(500);
    scene.run(1);
    CHECK_FALSE(attacker.knockedOut());
}

TEST_CASE("a knocked-out soldier whose death chain holds no flag takes no side in violence he sees", "[ai][events]") {
    // Expected: ai-brain-types.md#think-gang, confirmed (code): Brain_OnViolenceSeen (0x00291960) answers only while
    // HumanRecord_AreActionsBlocked (0x00228228) is false, and its mask holds the knocked-out and dying states: a human
    // lying in the loop 349, which holds no flag (docs/research/combat-reactions.md#knockout-timing), still takes no
    // side. Synthetic scene: neither human plays a clip, so neither holds a flag.
    AiScene scene;
    Brain& victim = member(scene, 19, "Punks", {44.0F, 40.0F, 0.0F});
    Brain& standing = scene.add({46.0F, 40.0F, 0.0F}, 0.0F);
    Brain& out = scene.add({44.0F, 42.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(victim.gang()->id(), standing);
    scene.brains.gangs().addMember(victim.gang()->id(), out);
    Brain& aggressor = member(scene, 21, "Others", {45.0F, 40.0F, 0.0F});
    out.human().fighter().health().set(0);
    scene.run(1);
    REQUIRE(out.knockedOut());
    REQUIRE(out.human().animator().flags() == 0);
    REQUIRE(standing.target() == nullptr);

    const coney::ai::BrainEvent seen{
        .id = coney::ai::kEventViolence, .other = &aggressor, .value = 0, .victim = &victim};
    coney::ai::onViolenceSeen(standing, seen);
    coney::ai::onViolenceSeen(out, seen);
    CHECK(standing.target() == &aggressor);
    CHECK(out.target() == nullptr);
    CHECK(out.goalCount() == 0);
}

TEST_CASE("Warriors who see two strange gangs fight take no side, and the strangers list them only once made enemies",
          "[ai][events]") {
    // Expected: ai-brain-types.md#help-calls, confirmed (code): Brain_PickSideInFight 0x002912b0
    // level34 checkpoint 5 (docs/research/scripting.md#level34-fixes, item 14): the Furies (kind 8) fight the hurt
    // Rogues (kind 4) the script made their enemies, next to the Warriors (kind 0), who are friends of neither. The
    // Warriors stay out of it (Brain_PickSideInFight: neither a friend hit nor a threat), and the Furies leave them be
    // until the script's GangMakeEnemies, after which a Fury's own scan lists them. This guards the brains' side of
    // issue #372 and passed before its fix too; the regression tests are combat_test.cpp's attack-warning cases and
    // disc_level34_furies_test.cpp.
    AiScene scene;
    scene.brains.setCollision(scene.mesh.get());
    Brain& fury = member(scene, 8, "Furies", {44.0F, 40.0F, 0.0F});
    Brain& rogue = member(scene, 4, "Rogues", {45.0F, 40.0F, 0.0F});
    const int warriorsGang = scene.brains.gangs().create(0, "Warriors");
    Brain& warrior = scene.add({44.0F, 48.0F, 0.0F}, 180.0F, coney::ai::BrainType::Warrior);
    scene.brains.gangs().addMember(warriorsGang, warrior);
    scene.brains.gangs().makeEnemies(fury.gang()->id(), rogue.gang()->id());
    scene.run(1);

    // The Rogue hits the Fury, who calls for help: the Warrior, 8 m off, hears it and does nothing.
    static_cast<void>(coney::ai::deliverEvent(
        fury, coney::ai::BrainEvent{.id = coney::ai::kEventDamaged, .other = &rogue, .value = 10}));
    coney::ai::broadcastHelpCall(fury, coney::ai::kHelpCallShared, &rogue);
    CHECK(warrior.target() == nullptr);
    CHECK(warrior.findGoal(GoalType::HelpRespond) == nullptr);
    CHECK_FALSE(coney::ai::Gangs::enemies(warrior.gang(), rogue.gang()));
    CHECK_FALSE(coney::ai::Gangs::enemies(warrior.gang(), fury.gang()));
    CHECK(coney::ai::scanEnemies(scene.brains.gangs(), fury) == 0);
    CHECK(std::ranges::find(fury.enemies(), &warrior) == fury.enemies().end());

    // The script turns the Furies: now the scan of the Fury, who faces him, lists the Warrior.
    scene.brains.gangs().makeEnemies(warriorsGang, fury.gang()->id());
    static_cast<void>(coney::ai::scanEnemies(scene.brains.gangs(), fury));
    CHECK(std::ranges::find(fury.enemies(), &warrior) != fury.enemies().end());
}

TEST_CASE("past 60 humans a help call reaches more than 60 friends in range", "[ai][events][crowd]") {
    // Expected: unverified, #1569 (Coney's own rule past the original's 60 humans: docs/engine/crowd-rules.md); up to
    // 60 humans Gang_BroadcastHelpCall 0x00293640 lists at most 60 hearers (ai-brain-types.md#help-calls), which
    // never binds
    AiScene scene;
    Brain& victim = member(scene, 19, "Punks", {44.0F, 40.0F, 0.0F});
    auto& gangs = scene.brains.gangs();
    std::vector<Brain*> mates;
    for (int g = 0; g < 8; ++g) {
        const int gang = gangs.create(19, "Mates " + std::to_string(g));
        gangs.makeFriends(gang, victim.gang()->id());
        for (int k = 0; k < 9; ++k) {
            mates.push_back(
                &scene.add({50.0F + static_cast<float>(k) * 0.5F, 42.0F + static_cast<float>(g), 0.0F}, 0.0F));
            gangs.addMember(gang, *mates.back());
        }
    }
    Brain& aggressor = member(scene, 21, "Others", {45.0F, 40.0F, 0.0F});
    REQUIRE(scene.brains.size() > 60);
    scene.run(1);

    coney::ai::broadcastHelpCall(victim, coney::ai::kHelpCallShared, &aggressor);
    // More take his side than a list of the original's 60 could reach (a mate busy with his own reaction may not).
    const auto sided = std::ranges::count_if(mates, [&](const Brain* mate) { return mate->target() == &aggressor; });
    CHECK(sided > static_cast<std::ptrdiff_t>(coney::crowd::kOrigMaxHumans));
}

TEST_CASE("under the engine limits preset a help call past 60 humans reaches at most the original's 60",
          "[ai][events][crowd][engine-limits]") {
    // Expected: ai-brain-types.md#help-calls, confirmed (code): Gang_BroadcastHelpCall 0x00293640 lists at most 60
    // hearers, in table order; the preset pins the lift at it (docs/guides/running-options.md#engine-limits)
    AiScene scene;
    scene.brains.setEngineLimits(coney::EngineLimits::Original);
    Brain& victim = member(scene, 19, "Punks", {44.0F, 40.0F, 0.0F});
    auto& gangs = scene.brains.gangs();
    std::vector<Brain*> mates;
    for (int g = 0; g < 8; ++g) {
        const int gang = gangs.create(19, "Mates " + std::to_string(g));
        gangs.makeFriends(gang, victim.gang()->id());
        for (int k = 0; k < 9; ++k) {
            mates.push_back(
                &scene.add({50.0F + static_cast<float>(k) * 0.5F, 42.0F + static_cast<float>(g), 0.0F}, 0.0F));
            gangs.addMember(gang, *mates.back());
        }
    }
    Brain& aggressor = member(scene, 21, "Others", {45.0F, 40.0F, 0.0F});
    REQUIRE(mates.size() > coney::crowd::kOrigMaxHumans);
    scene.run(1);

    coney::ai::broadcastHelpCall(victim, coney::ai::kHelpCallShared, &aggressor);
    const auto sided = std::ranges::count_if(mates, [&](const Brain* mate) { return mate->target() == &aggressor; });
    CHECK(sided <= static_cast<std::ptrdiff_t>(coney::crowd::kOrigMaxHumans));
    // The mates past the first 60 in table order are never listed, so none of them takes his side.
    CHECK(std::none_of(mates.begin() + static_cast<std::ptrdiff_t>(coney::crowd::kOrigMaxHumans), mates.end(),
                       [&](const Brain* mate) { return mate->target() == &aggressor; }));
}
