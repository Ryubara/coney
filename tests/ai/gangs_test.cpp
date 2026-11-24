// SPDX-License-Identifier: GPL-3.0-or-later
// The gangs (docs/research/ai-gangs.md#gangs, docs/research/gang-records.md): their records, enemies and friends, the
// brains' switches for a whole gang, the leader, and the events a member's gang hears before its brain.
#include "ai/gangs.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/idle_goals.h"
#include "ai/tactic.h"
#include "combat/meters.h"
#include "hud/hud.h"
#include "human/human.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::Gangs;
using coney::test::AiScene;

namespace {

// A tactic that counts its starts and updates.
class CountingTactic final : public coney::ai::Tactic {
  public:
    CountingTactic(int& starts, int& updates) : Tactic(0x30, ""), m_starts(&starts), m_updates(&updates) {}
    void start(coney::ai::Gang& /*gang*/) override { ++*m_starts; }
    int update(coney::ai::Gang& /*gang*/) override {
        ++*m_updates;
        return 0;
    }

  private:
    int* m_starts;
    int* m_updates;
};

} // namespace

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

TEST_CASE("joining a gang, the same one included, re-adds the brain; a gang at its count keeps every member",
          "[ai][gangs]") {
    // Expected: gang-records.md#join, confirmed (code): Gang_AddMember (0x00166308) has no early return and its
    // cap changes nothing.
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int a = gangs.create(19, "A");
    const int b = gangs.create(5, "B");
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(a, brain);
    brain.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    // Already in it: out and back in once; with no tactic its goals stay.
    gangs.addMember(a, brain);
    CHECK(brain.goalCount() == 1);
    CHECK(gangs.find(a)->members().size() == 1);
    gangs.addMember(b, brain);
    CHECK(brain.goalCount() == 1);
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
    CHECK(gangs.find(a)->members().size() == coney::ai::kGangMembers + 1);
    CHECK(joined.front()->gang() == gangs.find(a));
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
    // Only the hits count here: the fight it starts brings help calls (violence) too.
    const auto hits = [&scene] {
        std::vector<coney::ai::BrainEvent> found;
        for (const auto& [brain, event] : scene.services.humanEvents) {
            if (event.id == coney::ai::kEventDamaged) {
                found.push_back(event);
            }
        }
        return found;
    };
    REQUIRE(hits().size() == 1);
    const coney::ai::BrainEvent event = hits()[0];
    CHECK(event.id == coney::ai::kEventDamaged);
    CHECK(event.other == &near);
    CHECK(event.value == 20);
    scene.run(3);
    CHECK(hits().size() == 1); // once per fall
}

TEST_CASE("a lethal hit is event 1 with all the health it took, though the death chain leaves the victim 1",
          "[ai][gangs]") {
    // Expected: combat-reactions.md#down-health, confirmed (code): Human_DealDamage (0x0021d680) sends event 1 with
    // the damage it deals, and Human_PlayDeathReaction (0x00267a00) then writes 1 to the downed victim's health; the 1
    // is no health given back, so the event still carries all the health the hit took.
    AiScene scene;
    Brain& victim = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    victim.setServices(&scene.services);
    scene.run(3);
    coney::combat::Health& health = victim.human().fighter().health();
    const int full = health.value();
    // The lethal hit, then the death chain's write of 1.
    static_cast<void>(health.apply(full));
    health.keepOneWhileOut();
    REQUIRE(health.value() == 1);
    // The damage each event 1 sent so far carried.
    const auto damage = [&scene] {
        std::vector<int> found;
        for (const auto& [brain, event] : scene.services.humanEvents) {
            if (event.id == coney::ai::kEventDamaged) {
                found.push_back(event.value);
            }
        }
        return found;
    };
    scene.run(1);
    REQUIRE(damage().size() == 1);
    CHECK(damage()[0] == full);
    scene.run(3);
    CHECK(damage().size() == 1); // the 1 left is no new fall
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

namespace {

// A gang of five in `scene` and its id, the brains in `members` in this order: standing; knocked down alive (on the
// ground, health left); dead (no health); knocked out with health left; wounded with health left.
int makeCountedGang(AiScene& scene, std::vector<Brain*>& members) {
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(19, "Punks");
    for (int k = 0; k < 5; ++k) {
        Brain& brain = scene.add({44.0F + static_cast<float>(k), 40.0F, 0.0F}, 0.0F);
        gangs.addMember(gang, brain);
        members.push_back(&brain);
    }
    scene.run(2);
    // The second is hurt (below 35 % of 1400), then a heavy reaction (code 0x26) from the player in front knocks him
    // down with health left.
    members[1]->human().fighter().health().apply(1000);
    coney::human::IncomingHit hit;
    hit.damage = 46;
    hit.attackAnim = 13;
    hit.code = 0x26;
    hit.attacker = coney::anim::Vec3{40.0F, 40.0F, 0.0F};
    members[1]->human().hit(hit);
    for (int k = 0; k < 60 && !members[1]->human().fighter().victim().grounded(); ++k) {
        scene.run(1);
    }
    members[2]->human().fighter().health().set(0);
    members[3]->human().script().knockedOut = true;
    members[4]->human().script().wounded = true;
    return gang;
}

} // namespace

TEST_CASE("the living count takes every member whose handle resolves; standing leaves out the knocked out, wounded "
          "and dead, whatever their health",
          "[ai][gangs]") {
    // Research: docs/research/hud-functions-lua.md#gang-count-what (Gang_CountLiving(g, 0) against (g, 1))
    AiScene scene;
    std::vector<Brain*> members;
    const int id = makeCountedGang(scene, members);
    REQUIRE(members[1]->human().fighter().victim().grounded());
    REQUIRE_FALSE(members[1]->human().fighter().health().depleted());
    REQUIRE(members[2]->human().fighter().health().depleted());
    REQUIRE_FALSE(members[3]->human().fighter().health().depleted());
    REQUIRE_FALSE(members[4]->human().fighter().health().depleted());

    const coney::ai::Gang& gang = *scene.brains.gangs().find(id);
    CHECK(gang.countLiving(false) == 5);
    CHECK(gang.living() == 5);
    // Standing by state: the first, and the one knocked down alive (the mask has no knocked-down bit).
    CHECK(gang.countLiving(true) == 2);
    CHECK_FALSE(coney::ai::humanIsDownOrDead(*members[0]));
    CHECK_FALSE(coney::ai::humanIsDownOrDead(*members[1]));
    CHECK(coney::ai::humanIsDownOrDead(*members[2]));
    CHECK(coney::ai::humanIsDownOrDead(*members[3]));
    CHECK(coney::ai::humanIsDownOrDead(*members[4]));
    // The scripts' head count reads the same function.
    CHECK(scene.brains.gangs().countLiving(id, false) == 5);
    CHECK(scene.brains.gangs().countLiving(id, true) == 2);
    CHECK(scene.brains.gangs().countLiving(-1, false) == 0);
    CHECK(scene.brains.gangs().countLiving(31, true) == 0);
}

TEST_CASE("the HUD's gang counters show the standing members: a member knocked out with health left, or dead, loses "
          "his mark",
          "[ai][gangs][hud]") {
    // Research: docs/research/hud-functions-lua.md#gang-count-what (every HUD count passes 1)
    AiScene scene;
    std::vector<Brain*> members;
    const int id = makeCountedGang(scene, members);
    const coney::ai::Gangs& gangs = scene.brains.gangs();

    coney::hud::Hud hud;
    hud.setNumIndicator(2, true, id);
    hud.setNumIndicator(0, true, id); // slot 0 is fed by the same count
    coney::hud::HudFrame frame;
    frame.levelNumber = 101;
    frame.gangLiving = coney::ai::hudGangCount(gangs);
    hud.update(frame);
    CHECK(hud.numIndicator(2).count == 2);
    CHECK(hud.numIndicator(0).count == 2);

    // The knocked-out member wakes: his mark returns (he is not counted by health).
    members[3]->human().script().knockedOut = false;
    hud.update(frame);
    CHECK(hud.numIndicator(2).count == 3);
    // A member out of health is dead and never counts, whatever else his state is.
    members[1]->human().fighter().health().set(0);
    hud.update(frame);
    CHECK(hud.numIndicator(2).count == 2);
    // Gang -1 shows nothing.
    hud.setNumIndicator(1, true, -1);
    hud.update(frame);
    CHECK(hud.numIndicator(1).count == 0);
}

TEST_CASE("the standing count leaves out only the knocked out, dead and wounded: a gang whose last members are knocked "
          "down alive still counts them",
          "[ai][gangs]") {
    // Expected: characters.md#out-of-world, confirmed (code) at 0x00166220 (out only if knocked out, dead or wounded)
    AiScene scene;
    std::vector<Brain*> members;
    const int id = makeCountedGang(scene, members);
    const coney::ai::Gang& gang = *scene.brains.gangs().find(id);
    // The first and the one knocked down alive stand; the dead, the knocked out and the wounded do not.
    CHECK(gang.standing() == 2);

    // The one still up drops too, knocked out by a script: the member on the ground keeps the count at 1, so a
    // fight that waits for none standing goes on until he is out as well.
    members[0]->human().script().knockedOut = true;
    REQUIRE(members[1]->human().fighter().victim().grounded());
    CHECK(gang.standing() == 1);
    members[1]->human().script().wounded = true;
    CHECK(gang.standing() == 0);
}

TEST_CASE("a member whose human is deleted leaves the gang and drops the living count", "[ai][gangs]") {
    // The original's handle stops resolving at the delete; Coney's member leaves its gang (removeMember()).
    AiScene scene;
    std::vector<Brain*> members;
    const int id = makeCountedGang(scene, members);
    scene.brains.gangs().removeMember(*members[2]);
    CHECK(scene.brains.gangs().countLiving(id, false) == 4);
    CHECK(scene.brains.gangs().countLiving(id, true) == 2);
    scene.brains.gangs().removeMember(*members[1]);
    CHECK(scene.brains.gangs().countLiving(id, false) == 3);
    CHECK(scene.brains.gangs().countLiving(id, true) == 1);
}

TEST_CASE("a gang's capacity is the count Gang_AddMember tests, 10 and 16 for the police kinds, and holds no one back",
          "[ai][gangs]") {
    // Expected: gang-records.md#join, confirmed (code): Gang_AddMember (0x00166308) tests 16 for kinds 1 and
    // 0x17, else 10, and only calls a no-op past it; the 16 slots are the only bound.
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    struct Case {
        int kind;
        std::size_t limit;
    };
    for (const Case& c : {Case{coney::ai::kWarriorsKind, 10}, Case{19, 10}, Case{coney::ai::kPoliceKind, 16},
                          Case{coney::ai::kPoliceLikeKind, 16}}) {
        const int id = gangs.create(c.kind, "Kind" + std::to_string(c.kind));
        REQUIRE(id >= 0);
        CHECK(gangs.find(id)->capacity() == c.limit);
        // Two more than its count: each is listed while a slot is free.
        for (std::size_t i = 0; i < c.limit + 2; ++i) {
            gangs.addMember(id,
                            scene.add({10.0F + static_cast<float>(i), 10.0F + static_cast<float>(c.kind), 0.0F}, 0.0F));
        }
        CHECK(gangs.find(id)->members().size() == std::min(c.limit + 2, coney::ai::kGangMemberSlots));
    }
}

TEST_CASE("the gang's update without its spawners starts and runs its tactic once, only with members", "[ai][gangs]") {
    // Expected: ai-gangs.md#gang-update, confirmed (code): 0x00166708 processes the gang's tactic (Tactic_Process,
    // which starts it on the first call) when the gang has a tactic and members; a scene's end calls it with 0, which
    // skips the spawners (scenes.md#gang).
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    // A gang with a member, its tactic set after him (a member joining offers the tactic an event, which starts it).
    const int gang = gangs.create(coney::ai::kWarriorsKind, "Warriors");
    gangs.addMember(gang, scene.add({44.0F, 40.0F, 0.0F}, 0.0F));
    int starts = 0;
    int updates = 0;
    gangs.setTactic(gang, std::make_unique<CountingTactic>(starts, updates));
    coney::ai::Gang* warriors = gangs.find(gang);
    REQUIRE(warriors != nullptr);
    warriors->update();
    CHECK(starts == 1);
    CHECK(updates == 1);
    warriors->update();
    CHECK(starts == 1);
    CHECK(updates == 2);
    // A gang with no members: its tactic waits.
    const int empty = gangs.create(19, "Enemy");
    int emptyStarts = 0;
    int emptyUpdates = 0;
    gangs.setTactic(empty, std::make_unique<CountingTactic>(emptyStarts, emptyUpdates));
    gangs.find(empty)->update();
    CHECK(emptyStarts == 0);
    CHECK(emptyUpdates == 0);
}

TEST_CASE("a player leading the gang stays its leader while arrested or down, and leaves the lead only dead",
          "[ai][gangs]") {
    // Expected: gang-records.md#leader, confirmed (code): Gang_GetLeader (0x00165678) picks again only when the stored
    // leader is gone or dead, or is not a player and is cuffed or down.
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(coney::ai::kWarriorsKind, "Warriors");
    Brain& warrior = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(gang, warrior);
    gangs.addMember(gang, scene.player());
    coney::ai::Gang* warriors = gangs.find(gang);
    REQUIRE(warriors != nullptr);
    REQUIRE(warriors->orders().leader == scene.player().handle());
    CHECK(warriors->leader() == &scene.player());
    scene.player().human().script().arrested = true;
    CHECK(warriors->leader() == &scene.player());
    scene.player().human().script().arrested = false;
    scene.player().human().script().wounded = true;
    CHECK(warriors->leader() == &scene.player());
    // Dead (in Coney, out of health): the pick runs again, and player 1 in the gang, dead or not, leads first.
    scene.player().human().fighter().health().set(0);
    CHECK(warriors->leader() == &scene.player());
}

TEST_CASE("with no leader the gang picks player 1 or a cop first, then the first member free and up, then the first "
          "member, and keeps the pick",
          "[ai][gangs]") {
    // Expected: gang-records.md#leader, confirmed (code): Gang_PickLeader (0x00165738) stores and returns a member
    // who is player 1, or of class role 12 or 13 and not cuffed; else the first neither down nor cuffed; else the
    // first member at all.
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(19, "Punks");
    Brain& first = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& second = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    Brain& cop = scene.add({46.0F, 40.0F, 0.0F}, 0.0F);
    gangs.addMember(gang, first);
    gangs.addMember(gang, second);
    gangs.addMember(gang, cop);
    coney::ai::Gang* punks = gangs.find(gang);
    REQUIRE(punks != nullptr);
    // All three arrested and no cop: the first member all the same, kept as the leader.
    first.human().script().arrested = true;
    second.human().script().arrested = true;
    cop.human().script().arrested = true;
    CHECK(punks->leader() == &first);
    CHECK(punks->orders().leader == first.handle());
    // The second free again: the arrested leader is dropped for him.
    second.human().script().arrested = false;
    CHECK(punks->leader() == &second);
    // A cop's role (12), not cuffed, comes before any member free and up.
    gangs.setLookups(coney::ai::GangLookups{.role = [&cop](const Brain& brain) -> std::optional<int> {
        return &brain == &cop ? std::optional<int>(12) : std::nullopt;
    }});
    cop.human().script().arrested = false;
    punks->orders().leader = 0;
    CHECK(punks->leader() == &cop);
}

TEST_CASE("a full gang keeps every member: the new one takes the first free of its 16 slots, and with none he is "
          "in the gang but not listed",
          "[ai][gangs]") {
    // Expected: gang-records.md#join, confirmed (code): Gang_AddMember (0x00166308) only calls a no-op past its
    // cap and puts the member in the first free slot (HandleArray16_PutInFree), the brain's gang set either way.
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(19, "Punks");
    const int other = gangs.create(19, "Others");
    std::vector<Brain*> joined;
    for (int k = 0; k < 11; ++k) {
        Brain& member = scene.add({20.0F + static_cast<float>(k), 30.0F, 0.0F}, 0.0F);
        gangs.addMember(gang, member);
        joined.push_back(&member);
    }
    const coney::ai::Gang* punks = gangs.find(gang);
    REQUIRE(punks != nullptr);
    CHECK(punks->members().size() == 11);
    CHECK(joined.front()->gang() == punks);
    // The second leaves; the next to join fills his slot, second in the list.
    gangs.addMember(other, *joined[1]);
    Brain& late = scene.add({20.0F, 35.0F, 0.0F}, 0.0F);
    gangs.addMember(gang, late);
    REQUIRE(punks->members().size() == 11);
    CHECK(punks->members()[1] == &late);
    // All 16 slots taken: the 17th joins the gang but no slot holds him.
    for (int k = 0; k < 5; ++k) {
        gangs.addMember(gang, scene.add({20.0F + static_cast<float>(k), 50.0F, 0.0F}, 0.0F));
    }
    REQUIRE(punks->members().size() == coney::ai::kGangMemberSlots);
    Brain& extra = scene.add({30.0F, 50.0F, 0.0F}, 0.0F);
    gangs.addMember(gang, extra);
    CHECK(punks->members().size() == coney::ai::kGangMemberSlots);
    CHECK(extra.gang() == punks);
}

TEST_CASE("a member of character type 0xed brings his gang his own four cower clips", "[ai][gangs]") {
    // Expected: gang-records.md#join, confirmed (code): Gang_AddMember (0x00166308) sets group 0x29c from the
    // four names at 0x0050cb30 for a human of type 0xed.
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int gang = gangs.create(19, "Punks");
    Brain& gunman = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    gunman.setCharacterClass(0xed);
    gangs.addMember(gang, gunman);
    coney::ai::Gang* punks = gangs.find(gang);
    REQUIRE(punks != nullptr);
    CHECK(punks->clipGroup(coney::ai::kCowerClips) == 4);
    CHECK(punks->clip(coney::ai::kCowerClips, 0) == "l55_gun_aim");
    CHECK(punks->clip(coney::ai::kCowerClips, 3) == "l55_gun_wave");
}

TEST_CASE("a new gang of the police, police-like or dealer kind gets its kind's clip groups", "[ai][gangs]") {
    // Expected: gang-records.md#create, confirmed (code): Gang_Create (0x0016cdf0) sets group 0x29c for kind 1;
    // 0x29c, 0x25c and 0x253 for kind 0x17; 0x25b (eight) and 0x29c (three) for kind 0x18.
    Gangs gangs;
    coney::ai::Gang* police = gangs.find(gangs.create(coney::ai::kPoliceKind, "Police"));
    coney::ai::Gang* bums = gangs.find(gangs.create(coney::ai::kPoliceLikeKind, "Bums"));
    coney::ai::Gang* dealers = gangs.find(gangs.create(coney::ai::kNoFidgetsKind, "Dealers"));
    coney::ai::Gang* punks = gangs.find(gangs.create(19, "Punks"));
    REQUIRE(police != nullptr);
    REQUIRE(bums != nullptr);
    REQUIRE(dealers != nullptr);
    REQUIRE(punks != nullptr);
    CHECK(police->clip(coney::ai::kCowerClips, 0) == "cop_lost_target_search");
    CHECK(police->clip(coney::ai::kCowerClips, 3) == "cop_radio");
    CHECK(bums->clip(coney::ai::kCowerClips, 0) == "gen_cower_plead");
    CHECK(bums->clip(coney::ai::kIdleFidgetClips, 2) == "fidget_nose_wipe");
    CHECK(bums->clip(coney::ai::kTauntAdvanceClips, 3) == "taunt4_adv");
    CHECK(bums->clip(coney::ai::kTauntAdvanceClips, 4).empty());
    CHECK(dealers->clip(coney::ai::kCombatFidgetClips, 7) == "dlr_nearfight");
    CHECK(dealers->clip(coney::ai::kCowerClips, 2) == "money_take");
    CHECK(punks->clipGroup(coney::ai::kCowerClips) == 0);
}

TEST_CASE("a new police gang makes every wanted gang its enemy, and them its enemies unless the crime level is "
          "forced",
          "[ai][gangs]") {
    // Expected: gang-records.md#create, confirmed (code): Gang_Create (0x0016cdf0) calls Gang_SetHostileToKind
    // (0x0016c3a8) for each other wanted gang (+0x5e8) with the police kind, both ways only while game state +0x28c is
    // 0.
    Gangs gangs;
    bool forced = false;
    const int wanted = gangs.create(19, "Wanted");
    const int calm = gangs.create(19, "Calm");
    gangs.setLookups(coney::ai::GangLookups{.wanted = [wanted](int gang) { return gang == wanted; },
                                            .crimeLevelForced = [&forced] { return forced; }});
    const int police = gangs.create(coney::ai::kPoliceKind, "Police");
    CHECK(Gangs::enemies(gangs.find(police), gangs.find(wanted)));
    CHECK(Gangs::enemies(gangs.find(wanted), gangs.find(police)));
    CHECK_FALSE(Gangs::enemies(gangs.find(police), gangs.find(calm)));
    gangs.remove(police);
    forced = true;
    const int held = gangs.create(coney::ai::kPoliceKind, "Police");
    CHECK(Gangs::enemies(gangs.find(held), gangs.find(wanted)));
    CHECK_FALSE(Gangs::enemies(gangs.find(wanted), gangs.find(held)));
}

TEST_CASE("a gang made in a freed slot starts with no leader and the record's other orders reset", "[ai][gangs]") {
    // Expected: gang-records.md#create, confirmed (code): Gang_Create (0x0016cdf0) resets the record (0x00164808):
    // the leader the null handle, the turf boxes cleared, +0xd7 30, +0xd9 0.
    Gangs gangs;
    const int gang = gangs.create(19, "Punks");
    gangs.find(gang)->orders().leader = 7.0;
    gangs.find(gang)->orders().turf[0] = 3.0;
    gangs.find(gang)->orders().attackStrategies = true;
    gangs.remove(gang);
    const int again = gangs.create(19, "Others");
    REQUIRE(again == gang);
    const coney::ai::GangOrders& orders = gangs.find(again)->orders();
    CHECK(orders.leader == 0.0);
    CHECK(orders.turf[0] == 0.0);
    CHECK_FALSE(orders.attackStrategies);
    CHECK(orders.respondPercent == 30);
}

TEST_CASE("GangMakeEnemies and GangMakeFriends write a slot not in use too, so a gang made there inherits the bit",
          "[ai][gangs]") {
    // Expected: gang-records.md#relations, confirmed (code): GangMakeEnemies (0x0016acf0) and GangMakeFriends
    // (0x0016ad80) ignore only id -1 and write both records whether in use or not.
    Gangs gangs;
    const int warriors = gangs.create(coney::ai::kWarriorsKind, "Warriors");
    gangs.makeEnemies(warriors, 1);
    gangs.makeFriends(warriors, 2);
    const int punks = gangs.create(19, "Punks");
    const int friendly = gangs.create(21, "Friends");
    REQUIRE(punks == 1);
    REQUIRE(friendly == 2);
    CHECK(Gangs::enemies(gangs.find(warriors), gangs.find(punks)));
    CHECK(Gangs::friends(gangs.find(warriors), gangs.find(friendly)));
}
