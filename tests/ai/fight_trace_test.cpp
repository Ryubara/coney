// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_trace.h"

#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brain_events.h"
#include "ai/crew.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/idle_goals.h"
#include "ai/tactic.h"
#include "ai/tactic_attack.h"
#include "ai/targeting.h"
#include "support/ai_fixtures.h"

// The all-humans trace (--trace-humans): one row per human per update, the same columns for the player's gang and the
// others. Synthetic humans on a floor; game time is the steps run (1/30 s each).
// Research: docs/research/feel.md#method

namespace {

// The fields of one CSV line.
std::vector<std::string> fieldsOf(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    for (const char c : line) {
        if (c == ',' || c == '\n') {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    if (!field.empty()) {
        fields.push_back(field);
    }
    return fields;
}

// The lines of `text`.
std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line + "\n");
    }
    return lines;
}

// The index of the column called `name` in the header.
std::size_t columnOf(std::string_view name) {
    const std::vector<std::string> names = fieldsOf(coney::ai::FightTracer::header());
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return i;
        }
    }
    FAIL("no column " << name);
    return 0;
}

// The target changes `brain` counted under `cause`.
int changes(const coney::ai::Brain& brain, coney::ai::TargetCause cause) {
    return brain.targetChanges().at(static_cast<std::size_t>(cause));
}

// A gang tactic (type 0x20: offered new enemies) that answers a new enemy by setting the member's target to `pick`.
class PickingTactic final : public coney::ai::Tactic {
  public:
    explicit PickingTactic(coney::ai::Brain& pick) : Tactic(0x20, ""), m_pick(&pick) {}
    bool event(coney::ai::Gang& /*gang*/, coney::ai::Brain& member, const coney::ai::BrainEvent& event) override {
        if (event.id == coney::ai::kEventEnemyAdded) {
            member.setTarget(m_pick);
        }
        return false;
    }

  private:
    coney::ai::Brain* m_pick;
};

} // namespace

TEST_CASE("the all-humans trace header shares the player trace's names for the same quantities", "[ai][trace]") {
    const std::vector<std::string> names = fieldsOf(coney::ai::FightTracer::header());
    REQUIRE(!names.empty());
    CHECK(names.front() == "step");
    for (const char* shared :
         {"x", "y", "z", "heading", "speed", "vz", "gait", "clip", "command", "health", "stamina", "power"}) {
        std::size_t found = 0;
        for (const std::string& name : names) {
            found += name == shared ? 1 : 0;
        }
        INFO(shared);
        CHECK(found == 1);
    }
}

TEST_CASE("every human gets one row per update with as many fields as the header has columns", "[ai][trace]") {
    coney::test::AiScene scene;
    scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 90.0F);
    scene.add(coney::anim::Vec3{36.0F, 40.0F, 0.0F}, 270.0F);
    coney::ai::FightTracer tracer;
    const std::size_t columns = fieldsOf(coney::ai::FightTracer::header()).size();
    for (int step = 1; step <= 3; ++step) {
        scene.run(1);
        const std::vector<std::string> lines = linesOf(tracer.rows(static_cast<std::uint64_t>(step), scene.brains));
        REQUIRE(lines.size() == 3);
        for (std::size_t human = 0; human < lines.size(); ++human) {
            const std::vector<std::string> fields = fieldsOf(lines[human]);
            REQUIRE(fields.size() == columns);
            CHECK(fields[columnOf("step")] == std::to_string(step));
            CHECK(fields[columnOf("human")] == std::to_string(human));
        }
    }
}

TEST_CASE("the player's row says player, the others' do not, and the positions are the humans' feet", "[ai][trace]") {
    coney::test::AiScene scene;
    scene.add(coney::anim::Vec3{44.0F, 41.0F, 0.0F}, 90.0F);
    coney::ai::FightTracer tracer;
    scene.run(1);
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    REQUIRE(lines.size() == 2);
    const std::vector<std::string> player = fieldsOf(lines[0]);
    const std::vector<std::string> other = fieldsOf(lines[1]);
    CHECK(player[columnOf("player")] == "0"); // player 1
    CHECK(other[columnOf("player")] == "-1");
    CHECK(std::stof(other[columnOf("x")]) == 44.0F);
    CHECK(std::stof(other[columnOf("y")]) == 41.0F);
    CHECK(std::stoi(other[columnOf("health")]) > 0);
    CHECK(other[columnOf("down")] == "0");
    CHECK(other[columnOf("blocking")] == "0");
}

TEST_CASE("a fight shows in the goal stack, the target and its distance, and the attackers' slots", "[ai][trace]") {
    coney::test::AiScene scene;
    coney::ai::Brain& a = scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::Brain& b = scene.add(coney::anim::Vec3{30.0F, 33.0F, 0.0F}, 180.0F);
    const int gangA = scene.brains.gangs().create(19, "a");
    const int gangB = scene.brains.gangs().create(19, "b");
    scene.brains.gangs().addMember(gangA, a);
    scene.brains.gangs().addMember(gangB, b);
    scene.brains.gangs().makeEnemies(gangA, gangB);
    a.startFight(b);
    scene.run(2);
    coney::ai::FightTracer tracer;
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    REQUIRE(lines.size() == 3);
    const std::vector<std::string> row = fieldsOf(lines[1]);
    CHECK(row[columnOf("gang")] == std::to_string(gangA));
    CHECK(std::stoi(row[columnOf("goals")]) >= 1);
    CHECK(row[columnOf("goal")] != "-1");
    CHECK(row[columnOf("goal0")] != "-1");
    CHECK(row[columnOf("goal5")] == "-1"); // the stack is not six deep
    CHECK(row[columnOf("target")] == "2"); // b is the third human
    CHECK(std::stof(row[columnOf("target_dist")]) > 0.0F);
    CHECK(row[columnOf("slot_held")] == "1");
    const std::vector<std::string> victim = fieldsOf(lines[2]);
    CHECK(victim[columnOf("slots")] == "1");
    CHECK(row[columnOf("enemies")] != "0");
}

TEST_CASE("a human with no goals and no target traces -1 for them", "[ai][trace]") {
    coney::test::AiScene scene;
    scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    scene.run(1);
    coney::ai::FightTracer tracer;
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    const std::vector<std::string> row = fieldsOf(lines[1]);
    CHECK(row[columnOf("goal")] == "-1");
    CHECK(row[columnOf("goals")] == "0");
    CHECK(row[columnOf("target")] == "-1");
    CHECK(std::stof(row[columnOf("target_dist")]) == -1.0F);
    CHECK(row[columnOf("action_kind")] == "none");
    CHECK(row[columnOf("goal0")] == "-1");
    CHECK(row[columnOf("react")] == "-1");
    CHECK(row[columnOf("lock")] == "-1");
}

TEST_CASE("the header has the gang-battle columns of the original's field set and Coney's help counts", "[ai][trace]") {
    // research/traces/fields.toml [[humans]], docs/research/rumble.md#gang-battle-metrics.
    for (const char* name :
         {"state", "phase", "stunned", "attacking", "active", "space_up", "space_down", "fight_updates", "fight_active",
          "g_alert", "g_tactic", "g_teamups", "g_order", "brain_off", "help_calls", "helpers_sent", "target_causes"}) {
        INFO(name);
        (void)columnOf(name);
    }
}

TEST_CASE("target changes are counted by the cause named around them, and a same target is no change", "[ai][trace]") {
    coney::test::AiScene scene;
    coney::ai::Brain& a = scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::Brain& b = scene.add(coney::anim::Vec3{30.0F, 31.0F, 0.0F}, 180.0F);
    coney::ai::FightTracer tracer;
    CHECK(fieldsOf(linesOf(tracer.rows(1, scene.brains))[1])[columnOf("target_causes")].empty());
    {
        const coney::ai::TargetCauseScope cause(a, coney::ai::TargetCause::Pick);
        a.setTarget(&b);
        a.setTarget(&b); // the same target again: no change (docs/research/gang-fights.md#set-target-causes)
    }
    CHECK(a.targetCause() == coney::ai::TargetCause::Other); // the scope put the cause back
    a.setTarget(nullptr);
    a.setTarget(&b);
    static_cast<void>(a.knockOut()); // drops the target as a reaction to its own state
    const std::vector<std::string> row = fieldsOf(linesOf(tracer.rows(2, scene.brains))[1]);
    CHECK(row[columnOf("target_causes")] == "other=2;pick=1;reaction=1");
}

// The causes at their call sites (docs/research/gang-fights.md#set-target-causes): each test fails if the scope sits
// at the wrong call.

TEST_CASE("a hit's answer counts as hit_back on the soldier who was hit", "[ai][trace]") {
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Brain& soldier = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& hitter = scene.add(coney::anim::Vec3{45.0F, 40.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(scene.brains.gangs().create(19, "Punks"), soldier);
    scene.brains.gangs().addMember(scene.brains.gangs().create(21, "Others"), hitter);
    scene.run(1);
    static_cast<void>(coney::ai::deliverEvent(
        soldier, coney::ai::BrainEvent{.id = coney::ai::kEventDamaged, .other = &hitter, .value = 10}));
    REQUIRE(soldier.target() == &hitter);
    CHECK(changes(soldier, TargetCause::HitBack) == 1);
    CHECK(changes(soldier, TargetCause::Other) == 0);
}

TEST_CASE("a target a tactic sets while a hit back lists the enemy is the tactic's, not the hit back's",
          "[ai][trace]") {
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Brain& soldier = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& hitter = scene.add(coney::anim::Vec3{45.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& decoy = scene.add(coney::anim::Vec3{44.0F, 44.0F, 0.0F}, 0.0F);
    const int punks = scene.brains.gangs().create(19, "Punks");
    scene.brains.gangs().addMember(punks, soldier);
    scene.brains.gangs().addMember(scene.brains.gangs().create(21, "Others"), hitter);
    scene.run(1);
    scene.brains.gangs().setTactic(punks, std::make_unique<PickingTactic>(decoy));
    static_cast<void>(coney::ai::deliverEvent(
        soldier, coney::ai::BrainEvent{.id = coney::ai::kEventDamaged, .other = &hitter, .value = 10}));
    CHECK(soldier.target() == &hitter);
    CHECK(changes(soldier, TargetCause::Other) == 1);   // the tactic's pick of the decoy
    CHECK(changes(soldier, TargetCause::HitBack) == 1); // then the hitter
}

TEST_CASE("a heard help call's side counts as help on the hearer", "[ai][trace]") {
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Brain& victim = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& hearer = scene.add(coney::anim::Vec3{52.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& aggressor = scene.add(coney::anim::Vec3{45.0F, 40.0F, 0.0F}, 0.0F);
    const int punks = scene.brains.gangs().create(19, "Punks");
    scene.brains.gangs().addMember(punks, victim);
    scene.brains.gangs().addMember(punks, hearer);
    scene.brains.gangs().addMember(scene.brains.gangs().create(21, "Others"), aggressor);
    scene.run(1);
    coney::ai::broadcastHelpCall(victim, coney::ai::kHelpCallShared, &aggressor);
    REQUIRE(hearer.target() == &aggressor);
    CHECK(changes(hearer, TargetCause::Help) == 1);
    CHECK(changes(hearer, TargetCause::Other) == 0);
}

TEST_CASE("the fight goal's forced re-target counts as front, and the attacker it pushes out as displaced",
          "[ai][trace]") {
    using coney::ai::Brain;
    using coney::ai::TargetCause;
    // The scene of the re-target's own test (threats_test.cpp): the fighter behind the player turns on him and takes
    // his only slot from a nearer mate.
    coney::test::AiScene scene;
    coney::ai::Gangs& gangs = scene.brains.gangs();
    const int players = gangs.create(0, "Players");
    const int rogues = gangs.create(2, "Rogues");
    Brain& player = scene.player();
    Brain& fighter = scene.add(coney::anim::Vec3{40.0F, 38.9F, 0.0F}, 0.0F, coney::ai::BrainType::Gang);
    Brain& mate = scene.add(coney::anim::Vec3{40.0F, 40.6F, 0.0F}, 180.0F, coney::ai::BrainType::Gang);
    Brain& target = scene.add(coney::anim::Vec3{40.0F, 41.5F, 0.0F}, 180.0F, coney::ai::BrainType::Gang);
    gangs.addMember(players, player);
    gangs.addMember(players, target);
    gangs.addMember(rogues, fighter);
    gangs.addMember(rogues, mate);
    gangs.makeEnemies(players, rogues);
    for (Brain* calm : {&player, &mate, &target}) {
        calm->setThreatResponse(0);
    }
    player.setAttackSlotCount(1);
    mate.setTarget(&player);
    fighter.setTarget(&target);
    REQUIRE(fighter.pushGoal(std::make_unique<coney::ai::FightGoal>()));
    scene.run(32);
    REQUIRE(fighter.target() == &player);
    REQUIRE(mate.target() == nullptr);
    CHECK(changes(fighter, TargetCause::Front) >= 1);
    CHECK(changes(mate, TargetCause::Displaced) == 1);
    CHECK(changes(mate, TargetCause::Other) == 1); // only the test's own set
}

TEST_CASE("a tactic's start drops each AI member's target as an order", "[ai][trace]") {
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Brain& member = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& other = scene.add(coney::anim::Vec3{48.0F, 40.0F, 0.0F}, 0.0F);
    const int gang = scene.brains.gangs().create(19, "Crew");
    scene.brains.gangs().addMember(gang, member);
    member.setTarget(&other);
    scene.brains.gangs().setTactic(gang, std::make_unique<PickingTactic>(other));
    scene.run(1);
    CHECK(member.target() == nullptr);
    CHECK(changes(member, TargetCause::Order) == 1);
    CHECK(changes(member, TargetCause::Other) == 1); // the test's own set
}

TEST_CASE("FollowAndAttack's clear of a pick beyond 60 m of the leader counts with the pick", "[ai][trace]") {
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Brain& leader = scene.add(coney::anim::Vec3{2.0F, 2.0F, 0.0F}, 0.0F);
    coney::ai::Brain& member = scene.add(coney::anim::Vec3{75.0F, 75.0F, 0.0F}, 0.0F);
    coney::ai::Brain& enemy = scene.add(coney::anim::Vec3{77.0F, 75.0F, 0.0F}, 270.0F);
    member.addEnemy(enemy);
    REQUIRE(member.pushGoal(
        std::make_unique<coney::ai::FollowAndAttackGoal>(scene.services, scene.brains.formations(), leader.handle())));
    scene.run(10);
    CHECK(member.target() == nullptr);
    const int picks = changes(member, TargetCause::Pick);
    CHECK(picks >= 2);     // the pick's set of the enemy, then the clear
    CHECK(picks % 2 == 0); // each set has its clear
    CHECK(changes(member, TargetCause::Other) == 0);
}

TEST_CASE("the enemy pick's clear and its set both count as pick", "[ai][trace]") {
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Brain& member = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& enemy = scene.add(coney::anim::Vec3{46.0F, 40.0F, 0.0F}, 270.0F);
    member.addEnemy(enemy);
    member.setTarget(&enemy);
    REQUIRE(coney::ai::pickBestEnemy(member, nullptr, 0) == &enemy);
    CHECK(member.target() == &enemy);
    CHECK(changes(member, TargetCause::Pick) == 2);  // the clear, then the set of the same man
    CHECK(changes(member, TargetCause::Other) == 1); // the test's own set
}

TEST_CASE("a spectator's end drops his target as a pick", "[ai][trace]") {
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Brain& watcher = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& watched = scene.add(coney::anim::Vec3{50.0F, 40.0F, 0.0F}, 270.0F);
    watcher.setTarget(&watched);
    coney::ai::SpectateGoal spectate(coney::ai::SpectateArgs::melee(0.0F));
    spectate.end(watcher);
    CHECK(watcher.target() == nullptr);
    CHECK(changes(watcher, TargetCause::Pick) == 1);
    CHECK(changes(watcher, TargetCause::Other) == 1); // the test's own set
}

TEST_CASE("a reset drops the target as an order", "[ai][trace]") {
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Brain& soldier = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Brain& other = scene.add(coney::anim::Vec3{48.0F, 40.0F, 0.0F}, 0.0F);
    soldier.setTarget(&other);
    CHECK(coney::ai::answerEvent(soldier, coney::ai::BrainEvent{.id = coney::ai::kEventReset}));
    CHECK(soldier.target() == nullptr);
    CHECK(changes(soldier, TargetCause::Order) == 1);
    CHECK(changes(soldier, TargetCause::Other) == 1); // the test's own set
}

TEST_CASE("the attack tactic's members sent to violence in view count the new target as help", "[ai][trace]") {
    using coney::ai::Brain;
    using coney::ai::TargetCause;
    coney::test::AiScene scene;
    coney::ai::Gangs& gangs = scene.brains.gangs();
    Brain& leader = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& idle = scene.add(coney::anim::Vec3{45.0F, 40.0F, 0.0F}, 0.0F);
    Brain& aggressor = scene.add(coney::anim::Vec3{50.0F, 40.0F, 0.0F}, 0.0F);
    const int gang = gangs.create(19, "Furies");
    gangs.addMember(gang, leader);
    gangs.addMember(gang, idle);
    const int other = gangs.create(21, "Orphans");
    gangs.addMember(other, aggressor);
    gangs.makeEnemies(gang, other);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    coney::ai::Gang& furies = *gangs.find(gang);
    idle.clearGoals();
    const int before = changes(idle, TargetCause::Help);
    const coney::ai::BrainEvent hit{
        .id = coney::ai::kEventViolence, .other = &aggressor, .value = 0, .victim = &leader};
    CHECK_FALSE(furies.tactic()->event(furies, leader, hit));
    REQUIRE(idle.target() == &aggressor);
    CHECK(changes(idle, TargetCause::Help) == before + 1);
}

TEST_CASE("a fight goal on top traces its updates and its place, and its target the places and spacing on him",
          "[ai][trace]") {
    coney::test::AiScene scene;
    coney::ai::Brain& a = scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::Brain& b = scene.add(coney::anim::Vec3{30.0F, 31.0F, 0.0F}, 180.0F);
    const int gangA = scene.brains.gangs().create(19, "a");
    const int gangB = scene.brains.gangs().create(19, "b");
    scene.brains.gangs().addMember(gangA, a);
    scene.brains.gangs().addMember(gangB, b);
    scene.brains.gangs().makeEnemies(gangA, gangB);
    b.setThreatResponse(0); // b never fights back, so only a has a fight goal
    a.startFight(b);
    scene.run(5);
    coney::ai::FightTracer tracer;
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    REQUIRE(lines.size() == 3);
    const std::vector<std::string> fighter = fieldsOf(lines[1]);
    REQUIRE(fighter[columnOf("goal")] == "15");
    CHECK(std::stoi(fighter[columnOf("fight_updates")]) >= 1);
    const std::string& place = fighter[columnOf("fight_active")];
    CHECK((place == "0" || place == "1"));
    const std::vector<std::string> victim = fieldsOf(lines[2]);
    CHECK(victim[columnOf("fight_updates")] == "-1"); // no fight goal on top
    CHECK(victim[columnOf("fight_active")] == "-1");
    CHECK(victim[columnOf("active")] == place); // a's place, if he holds one, is on b
    CHECK(std::stoi(victim[columnOf("space_up")]) >= 1);
    CHECK(std::stoi(victim[columnOf("space_down")]) >= 1);
    CHECK(fighter[columnOf("brain_off")] == "0");
    CHECK(fighter[columnOf("g_teamups")] == "0"); // GangEnableAttackStrategies is 0 after a reset
    CHECK(fighter[columnOf("g_order")] == "-1");  // no team-up order runs
    CHECK(fighter[columnOf("g_tactic")] == "0");  // no tactic
}

TEST_CASE("the attack phase and the state word come from the human's held flags and states", "[ai][trace]") {
    coney::test::AiScene scene;
    coney::ai::Brain& b = scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    scene.run(1);
    coney::ai::FightTracer tracer;
    std::vector<std::string> row = fieldsOf(linesOf(tracer.rows(1, scene.brains))[1]);
    CHECK(row[columnOf("phase")] == std::to_string(b.human().heldFlags()));
    CHECK(row[columnOf("attacking")] == "0");
    CHECK(row[columnOf("state")] == "0");
    CHECK(row[columnOf("stunned")] == "0");
    // Wounded is the state's 0x10000 (docs/research/rumble.md#gang-battle-results); knocked out 0x40000
    // (docs/research/combat-reactions.md#knocked-out-bit).
    b.human().setWounded(true);
    (void)b.knockOut();
    row = fieldsOf(linesOf(tracer.rows(2, scene.brains))[1]);
    const auto state = std::stoul(row[columnOf("state")]);
    CHECK((state & 0x10000U) != 0);
    CHECK((state & 0x40000U) != 0);
}

TEST_CASE("a dead brain traces brain_off 1", "[ai][trace]") {
    coney::test::AiScene scene;
    coney::ai::Brain& b = scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    b.setDead(true);
    coney::ai::FightTracer tracer;
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    CHECK(fieldsOf(lines[0])[columnOf("brain_off")] == "0");
    CHECK(fieldsOf(lines[1])[columnOf("brain_off")] == "1");
}

TEST_CASE("a gang's tactic is traced as the original's vtable of the tactics the research names, and its team-ups",
          "[ai][trace]") {
    coney::test::AiScene scene;
    coney::ai::Brain& a = scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    const int gang = scene.brains.gangs().create(19, "a");
    scene.brains.gangs().addMember(gang, a);
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::TacticAttack>(""));
    coney::ai::Gang* found = scene.brains.gangs().find(gang);
    REQUIRE(found != nullptr);
    found->orders().attackStrategies = true;
    coney::ai::FightTracer tracer;
    const std::vector<std::string> row = fieldsOf(linesOf(tracer.rows(1, scene.brains))[1]);
    CHECK(row[columnOf("g_tactic")] == std::to_string(0x00543320)); // TacticAttack (gang-fights.md#runtime)
    CHECK(row[columnOf("g_teamups")] == "1");
}

TEST_CASE("help calls and helpers sent are counted on the human who called or was sent", "[ai][trace]") {
    coney::test::AiScene scene;
    coney::ai::Brain& a = scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::Brain& b = scene.add(coney::anim::Vec3{30.0F, 32.0F, 0.0F}, 0.0F);
    coney::ai::Brain& c = scene.add(coney::anim::Vec3{32.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::broadcastHelpCall(a, coney::ai::kHelpCallShared, &b);
    coney::ai::sendHelper(c, b);
    coney::ai::FightTracer tracer;
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    REQUIRE(lines.size() == 4);
    CHECK(fieldsOf(lines[1])[columnOf("help_calls")] == "1"); // one call, whoever hears it
    CHECK(fieldsOf(lines[1])[columnOf("helpers_sent")] == "0");
    CHECK(fieldsOf(lines[3])[columnOf("helpers_sent")] == "1");
    CHECK(fieldsOf(lines[3])[columnOf("help_calls")] == "0");
}

TEST_CASE("a human keeps its id when another leaves the scene", "[ai][trace]") {
    coney::test::AiScene scene;
    scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::Brain& last = scene.add(coney::anim::Vec3{50.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::FightTracer tracer;
    scene.run(1);
    (void)tracer.rows(1, scene.brains);
    const coney::human::Human& middle = scene.brains.at(1).human();
    scene.step.remove(middle);
    scene.brains.remove(middle);
    scene.run(1);
    const std::vector<std::string> lines = linesOf(tracer.rows(2, scene.brains));
    REQUIRE(lines.size() == 2);
    CHECK(fieldsOf(lines[1])[columnOf("human")] == "2");
    CHECK(last.human().position().x == 50.0F);
}
