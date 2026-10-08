// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brain.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/attack_action.h"
#include "ai/attack_kinds.h"
#include "ai/attack_places.h"
#include "ai/block_goal.h"
#include "ai/brains.h"
#include "ai/fight_goal.h"
#include "ai/goal.h"
#include "ai/reaction_goals.h"
#include "combat/ai_counter.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/reactions.h"
#include "human/human.h"
#include "human/humans.h"
#include "human/locomotion_gate.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The brain shell, its goal stack and action queue, the think stagger, the attack action's pacing and press, the block
// goal and the reaction goals (docs/research/ai.md). Synthetic clips only; game time is the steps run (1/30 s each).

using coney::ai::Brain;
using coney::ai::BrainType;
using coney::human::Human;
namespace command = coney::combat::command;

namespace {

// One step of game time, ms, as Brains counts it.
std::uint64_t stepMs(std::uint64_t step) { return step * 1000 / 30; }

// A goal that logs its hooks into `log` and returns `status` from process(); it keeps the attack warnings it saw.
class LoggingGoal final : public coney::ai::Goal {
  public:
    LoggingGoal(std::string name, std::vector<std::string>& log)
        : Goal(coney::ai::GoalType::Fight), m_name(std::move(name)), m_log(&log) {}
    void start(Brain& /*brain*/) override { m_log->push_back(m_name + " start"); }
    void resume(Brain& /*brain*/) override { m_log->push_back(m_name + " resume"); }
    void suspend(Brain& /*brain*/) override { m_log->push_back(m_name + " suspend"); }
    void end(Brain& /*brain*/) override { m_log->push_back(m_name + " end"); }
    coney::ai::GoalStatus process(Brain& brain) override {
        m_log->push_back(m_name + " process");
        warnings.push_back(brain.attackWarnings());
        return status;
    }
    coney::ai::GoalStatus status = coney::ai::GoalStatus::Stop;
    std::vector<int> warnings;

  private:
    std::string m_name;
    std::vector<std::string>* m_log;
};

// An action that logs its start and updates into `log`, is done after `updates` updates, and may refuse to abort.
class LoggingAction final : public coney::ai::Action {
  public:
    LoggingAction(std::string name, std::vector<std::string>& log, std::int16_t delayMs = 0, int updates = 1000,
                  bool refuses = false)
        : Action(delayMs), m_name(std::move(name)), m_log(&log), m_updates(updates), m_refuses(refuses) {}
    coney::ai::ActionStatus start(Brain& brain) override {
        m_log->push_back(m_name + " start@" + std::to_string(brain.nowMs()));
        return coney::ai::ActionStatus::Running;
    }
    coney::ai::ActionStatus update(Brain& /*brain*/) override {
        m_log->push_back(m_name + " update");
        return --m_updates <= 0 ? coney::ai::ActionStatus::Done : coney::ai::ActionStatus::Running;
    }
    bool abort(Brain& /*brain*/) override { return !m_refuses; }

  private:
    std::string m_name;
    std::vector<std::string>* m_log;
    int m_updates;
    bool m_refuses;
};

// A 30-damage S1 from a human standing at (40, 40), with the range flags `flags`.
coney::human::IncomingHit strikeFromFront(std::uint16_t flags) {
    coney::human::IncomingHit hit;
    hit.damage = 30;
    hit.attackAnim = 12;
    hit.code = 0x0a;
    hit.flags = flags;
    hit.attacker = coney::anim::Vec3{40.0F, 40.0F, 0.0F};
    return hit;
}

// The synthetic fighting character's humans on a floor: slot 0 the pad's player at (40, 40) facing +y, then AI humans
// on side 1, each with a brain, stepped through the characters' step with the brains at their place.
struct Scene {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    std::vector<std::unique_ptr<Human>> humans;
    coney::human::Humans step;
    coney::ai::Brains brains;

    Scene() {
        add(coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F, BrainType::Player);
        step.setBrains(brains.hook());
    }

    // A human with its feet at `feet` facing `headingDegrees`, with a brain of `type` and the Warriors' power class.
    Brain& add(coney::anim::Vec3 feet, float headingDegrees, BrainType type) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        const bool player = type == BrainType::Player;
        made.setFighterProfile(coney::human::FighterProfile{
            .player = player, .powerClass = coney::ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), feet, headingDegrees);
        step.add(made, player);
        return brains.add(made, type, coney::ai::FightSettings{}, static_cast<std::uint32_t>(humans.size()));
    }

    Human& player() { return *humans.front(); }
    Brain& playerBrain() { return brains.at(0); }
    void run(int steps) {
        for (int k = 0; k < steps; ++k) {
            step.update(mesh.get());
        }
    }
};

} // namespace

TEST_CASE("a goal starts only once the action queue is empty, and a push suspends the top and clears the actions",
          "[ai]") {
    Scene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F, BrainType::Gang);
    std::vector<std::string> log;
    REQUIRE(brain.queueAction(std::make_unique<LoggingAction>("a", log, 0, 2)));
    REQUIRE(brain.pushGoal(std::make_unique<LoggingGoal>("g", log)));
    // Pushing a goal clears the queue, so the action queued before it is gone; queue one after.
    CHECK(brain.actionCount() == 0);
    REQUIRE(brain.queueAction(std::make_unique<LoggingAction>("b", log, 0, 2)));
    brain.update(stepMs(1));
    // The goal waits for the queue: the action starts and runs, the goal does not.
    CHECK(log == std::vector<std::string>{"b start@33", "b update"});
    brain.update(stepMs(2));
    brain.update(stepMs(3));
    CHECK(log == std::vector<std::string>{"b start@33", "b update", "b update", "g start", "g process"});

    // A second goal suspends the first; when it is popped, the first is resumed before its next process.
    log.clear();
    REQUIRE(brain.pushGoal(std::make_unique<LoggingGoal>("h", log)));
    brain.update(stepMs(4));
    brain.popGoal();
    brain.update(stepMs(5));
    CHECK(log == std::vector<std::string>{"g suspend", "h start", "h process", "h end", "g resume", "g process"});
    brain.popGoal();
    CHECK(brain.goalCount() == 0);
    CHECK(brain.goalsRanOut() == 1);
}

// A goal whose End pushes the next goal, as a script handler the End reaches may.
class PushingGoal final : public coney::ai::Goal {
  public:
    PushingGoal(std::vector<std::string>& log) : Goal(coney::ai::GoalType::Fight), m_log(&log) {}
    void end(Brain& brain) override {
        m_log->push_back("pusher end");
        REQUIRE(brain.pushGoal(std::make_unique<LoggingGoal>("next", *m_log)));
    }
    coney::ai::GoalStatus process(Brain& /*brain*/) override { return coney::ai::GoalStatus::Stop; }

  private:
    std::vector<std::string>* m_log;
};

TEST_CASE("a goal pushed from a popped goal's End stays on the stack", "[ai]") {
    Scene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F, BrainType::Gang);
    std::vector<std::string> log;
    REQUIRE(brain.pushGoal(std::make_unique<LoggingGoal>("base", log)));
    REQUIRE(brain.pushGoal(std::make_unique<PushingGoal>(log)));
    REQUIRE(brain.goalCount() == 2);
    brain.popGoal();
    // The pusher is gone and the goal its End pushed is on top of the base, not popped in the pusher's place.
    CHECK(brain.goalCount() == 2);
    log.clear();
    brain.update(stepMs(1));
    CHECK(log == std::vector<std::string>{"next start", "next process"});
}

TEST_CASE("a done goal is popped and the new top is processed in the same update", "[ai]") {
    Scene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F, BrainType::Gang);
    std::vector<std::string> log;
    REQUIRE(brain.pushGoal(std::make_unique<LoggingGoal>("low", log)));
    auto high = std::make_unique<LoggingGoal>("high", log);
    high->status = coney::ai::GoalStatus::Done;
    REQUIRE(brain.pushGoal(std::move(high)));
    brain.update(stepMs(1));
    CHECK(log == std::vector<std::string>{"low suspend", "high start", "high process", "high end", "low start",
                                          "low process"});
    CHECK(brain.goalCount() == 1);
}

TEST_CASE("the stack holds ten goals and the queue eight actions; a refused abort keeps the actions from it on",
          "[ai]") {
    Scene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F, BrainType::Gang);
    std::vector<std::string> log;
    for (std::size_t k = 0; k < coney::ai::kGoalStackSize; ++k) {
        REQUIRE(brain.pushGoal(std::make_unique<LoggingGoal>("g", log)));
    }
    CHECK_FALSE(brain.pushGoal(std::make_unique<LoggingGoal>("g", log)));
    brain.flush();
    CHECK(brain.goalCount() == 0);

    REQUIRE(brain.queueAction(std::make_unique<LoggingAction>("free", log)));
    REQUIRE(brain.queueAction(std::make_unique<LoggingAction>("busy", log, 0, 1000, true)));
    for (std::size_t k = 2; k < coney::ai::kActionQueueSize; ++k) {
        REQUIRE(brain.queueAction(std::make_unique<LoggingAction>("more", log)));
    }
    CHECK_FALSE(brain.queueAction(std::make_unique<LoggingAction>("over", log)));
    // Clearing frees from the front and stops at the action that refuses (an attack under way is never cut).
    brain.clearActions();
    CHECK(brain.actionCount() == coney::ai::kActionQueueSize - 1);
}

TEST_CASE("an action's delay counts down against the time of the brain's last update", "[ai]") {
    Scene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F, BrainType::Gang);
    std::vector<std::string> log;
    REQUIRE(brain.queueAction(std::make_unique<LoggingAction>("a", log, 100)));
    for (std::uint64_t step = 1; step <= 4; ++step) {
        brain.update(stepMs(step));
    }
    // 33 + 33 + 34 ms: the third update's elapsed time ends the 100 ms delay.
    REQUIRE(!log.empty());
    CHECK(log.front() == "a start@100");
}

TEST_CASE("each brain thinks one character step in five, staggered by its slot, and updates every step", "[ai]") {
    Scene scene;
    for (int k = 0; k < 6; ++k) {
        scene.add({30.0F + static_cast<float>(k), 30.0F, 0.0F}, 0.0F, BrainType::Gang);
    }
    std::vector<std::vector<std::uint64_t>> thinks(scene.brains.size());
    for (std::uint64_t step = 1; step <= 20; ++step) {
        std::vector<std::uint64_t> before(scene.brains.size());
        for (std::size_t i = 0; i < scene.brains.size(); ++i) {
            before[i] = scene.brains.at(i).thinks();
        }
        scene.brains.update();
        for (std::size_t i = 0; i < scene.brains.size(); ++i) {
            if (scene.brains.at(i).thinks() != before[i]) {
                thinks[i].push_back(step);
            }
        }
    }
    for (std::size_t i = 0; i < scene.brains.size(); ++i) {
        INFO("brain " << i);
        REQUIRE(thinks[i].size() == 4);
        for (const std::uint64_t step : thinks[i]) {
            CHECK(step % coney::ai::kThinkPeriod == i % coney::ai::kThinkPeriod);
        }
        CHECK(scene.brains.at(i).updates() == 20);
    }
}

TEST_CASE("a Warrior waits the attack delay times its class's factor, halved when its target targets it", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({41.0F, 40.0F, 0.0F}, 270.0F, BrainType::Gang);
    // 200 ms × 20 = 4 s; a kind of 1000 ms × 20; halved, 2 s.
    CHECK(gang.attackDelayMs(1, false, false) == 4000);
    CHECK(gang.attackDelayMs(20, false, false) == 20000);
    CHECK(gang.attackDelayMs(1, false, true) == 2000);

    // The attack action: the next attack is now + the delay; the target's is now + the kind's swing time over his
    // spacing (1).
    gang.setTarget(&scene.playerBrain());
    REQUIRE(gang.queueAction(std::make_unique<coney::ai::AttackAction>(1, 0)));
    gang.update(stepMs(30));
    CHECK(gang.nextAttackMs() == stepMs(30) + 4000);
    CHECK(scene.playerBrain().attackableAtMs() ==
          stepMs(30) + static_cast<std::uint64_t>(coney::ai::swingTimeMs(1, false, gang.human().anims())));
    // Targeted back by the target's brain: halved.
    scene.playerBrain().setTarget(&gang);
    REQUIRE(gang.queueAction(std::make_unique<coney::ai::AttackAction>(1, 0)));
    gang.update(stepMs(31));
    CHECK(gang.nextAttackMs() == stepMs(31) + 2000);
    // A type-3 brain (an ally Warrior): always halved.
    Brain& ally = scene.add({39.0F, 40.0F, 0.0F}, 90.0F, BrainType::Warrior);
    ally.setTarget(&gang);
    REQUIRE(ally.queueAction(std::make_unique<coney::ai::AttackAction>(1, 0)));
    ally.update(stepMs(32));
    CHECK(ally.nextAttackMs() == stepMs(32) + 2000);
}

TEST_CASE("an attack on a busy target leaves the target's attackable time alone", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({41.0F, 40.0F, 0.0F}, 270.0F, BrainType::Gang);
    gang.setTarget(&scene.playerBrain());
    // Stunned, the player is busy by his state alone (no record bit): the strike leaves +0x1ec where it was, so the
    // others do not wait out swings landed while he could not answer.
    scene.player().stunFor(stepMs(30), 5000);
    REQUIRE((scene.player().animator().flags() & coney::human::kBusyFlags) == 0);
    REQUIRE(coney::ai::humanBusy(scene.player()));
    const std::uint64_t before = scene.playerBrain().attackableAtMs();
    REQUIRE(gang.queueAction(std::make_unique<coney::ai::AttackAction>(1, 0)));
    gang.update(stepMs(30));
    CHECK(gang.nextAttackMs() == stepMs(30) + 4000);
    CHECK(scene.playerBrain().attackableAtMs() == before);
}

TEST_CASE("the attack action writes its command once, as it starts, and waits on the attack's held flags", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.0F, 0.0F}, 180.0F, BrainType::Gang);
    gang.setTarget(&scene.playerBrain());
    REQUIRE(gang.queueAction(std::make_unique<coney::ai::AttackAction>(1, 0)));
    // What the record holds once the brains have run, each step.
    std::vector<coney::combat::CommandId> written;
    scene.step.setBrains([&scene, &written, &gang](std::span<Human* const> /*humans*/) {
        scene.brains.update();
        written.push_back(gang.human().record().command);
    });
    bool playedS1 = false;
    for (int k = 0; k < 20; ++k) {
        scene.step.update(scene.mesh.get());
        playedS1 = playedS1 ||
                   gang.human().animator().animId() == static_cast<std::uint32_t>(coney::combat::anim_id::kAttackS1);
    }
    REQUIRE(written.size() == 20);
    CHECK(written[0] == command::kSquarePressed);
    for (std::size_t k = 1; k < written.size(); ++k) {
        CHECK(written[k] == command::kNone);
    }
    // The square started S1 through the dispatcher.
    CHECK(playedS1);
}

TEST_CASE("a brain counts the attack starts its range and field of view take in", "[ai]") {
    Scene scene;
    // The AI at (40, 50) facing +y (heading 0).
    Brain& gang = scene.add({40.0F, 50.0F, 0.0F}, 0.0F, BrainType::Gang);
    std::vector<std::string> log;
    auto goal = std::make_unique<LoggingGoal>("g", log);
    LoggingGoal& seen = *goal;
    REQUIRE(gang.pushGoal(std::move(goal)));
    Human& ai = gang.human();
    ai.announceAttack({40.0F, 52.0F, 0.0F}); // ahead, 2 m: counted
    ai.announceAttack({42.0F, 50.5F, 0.0F}); // to the side, within 1.92 rad: counted
    ai.announceAttack({40.0F, 48.0F, 0.0F}); // behind (π off): not counted
    ai.announceAttack({40.0F, 85.0F, 0.0F}); // ahead, 35 m: beyond the 30 m range
    gang.update(stepMs(1));
    gang.update(stepMs(2));
    REQUIRE(seen.warnings.size() == 2);
    CHECK(seen.warnings[0] == 2);
    // Counted per update: none were announced since.
    CHECK(seen.warnings[1] == 0);
    CHECK(gang.attackWarnings() == 0);
}

TEST_CASE("a brain does not count an attack start it has no line of sight to", "[ai][sight]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 50.0F, 0.0F}, 0.0F, BrainType::Gang);
    // A wall 1 m ahead of the AI, between it and the attacker 2 m ahead.
    const auto walled =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusY(51.0F, 30.0F, 50.0F, 0.0F, 4.0F, 0, 5)));
    gang.setCollision(walled.get());
    std::vector<std::string> log;
    auto goal = std::make_unique<LoggingGoal>("g", log);
    LoggingGoal& seen = *goal;
    REQUIRE(gang.pushGoal(std::move(goal)));
    gang.human().announceAttack({40.0F, 52.0F, 0.0F});
    gang.update(stepMs(1));
    REQUIRE(seen.warnings.size() == 1);
    CHECK(seen.warnings[0] == 0);
    // In the open the same start is counted.
    gang.setCollision(scene.mesh.get());
    gang.human().announceAttack({40.0F, 52.0F, 0.0F});
    gang.update(stepMs(2));
    CHECK(seen.warnings[1] == 1);
}

TEST_CASE("an AI's block goal writes R1 held but never blocks; it has no pad", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.5F, 0.0F}, 180.0F, BrainType::Gang);
    gang.setTarget(&scene.playerBrain());
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::BlockGoal>()));
    std::vector<coney::combat::CommandId> written;
    scene.step.setBrains([&scene, &written, &gang](std::span<Human* const> /*humans*/) {
        scene.brains.update();
        written.push_back(gang.human().record().command);
    });
    bool blocked = false;
    for (int k = 0; k < 25; ++k) {
        scene.step.update(scene.mesh.get());
        blocked = blocked || gang.human().fighter().combat().blocking();
        CHECK(gang.human().record().buttons == 0);
    }
    // The player stands idle: no grab or tackle to counter, so R1 held on every update of the block.
    REQUIRE(written.size() == 25);
    for (const coney::combat::CommandId command : written) {
        CHECK(command == command::kR1Held);
    }
    CHECK_FALSE(blocked);
}

TEST_CASE("the block goal turns hit reactions off until its sixth update and ends one update after its time", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 45.0F, 0.0F}, 180.0F, BrainType::Gang);
    gang.setTarget(&scene.playerBrain());
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::BlockGoal>()));
    const auto* block = dynamic_cast<const coney::ai::BlockGoal*>(gang.topGoal());
    REQUIRE(block != nullptr);
    std::uint64_t step = 1;
    gang.update(stepMs(step));
    // 1-3 s from the start.
    CHECK(block->untilMs() >= stepMs(1) + coney::ai::kBlockMinMs);
    CHECK(block->untilMs() <= stepMs(1) + coney::ai::kBlockMaxMs);
    for (; step < coney::ai::kBlockReactionsBackUpdate; ++step) {
        CHECK(gang.human().fighter().hitReactionsOff());
        gang.update(stepMs(step + 1));
    }
    CHECK_FALSE(gang.human().fighter().hitReactionsOff());
    // The target not attacking: the update after the time has run out ends the block, and the next one pops it.
    const std::uint64_t until = block->untilMs();
    while (stepMs(step) <= until) {
        gang.update(stepMs(++step));
    }
    CHECK(gang.goalCount() == 1);
    CHECK_FALSE(block->active());
    CHECK_FALSE(block->patterned());
    gang.update(stepMs(++step));
    CHECK(gang.goalCount() == 0);
}

TEST_CASE("a block goal without a target leaves the hit reactions on", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 45.0F, 0.0F}, 180.0F, BrainType::Gang);
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::BlockGoal>()));
    gang.update(stepMs(1));
    CHECK_FALSE(gang.human().fighter().hitReactionsOff());
    CHECK(gang.human().record().command == command::kR1Held);
}

TEST_CASE("a hit with reactions off takes health and plays no reaction", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.0F, 0.0F}, 180.0F, BrainType::Gang);
    Human& ai = gang.human();
    scene.run(2);
    const std::uint32_t idle = ai.animator().animId();
    ai.fighter().setHitReactionsOff(true);
    ai.hit(strikeFromFront(0));
    scene.run(1);
    CHECK(ai.fighter().health().value() == 1370);
    CHECK(ai.animator().animId() == idle);
}

namespace {

// Whether `human` plays a grab's intro (69-71).
bool inGrabIntro(const Human& human) {
    const auto clip = static_cast<int>(human.animator().animId());
    return clip >= coney::combat::anim_id::kGrabMiss && clip <= coney::combat::anim_id::kGrabPlayerIntro;
}

} // namespace

TEST_CASE("the counter test passes only for a Warrior's brain against a grab's or a tackle's intro aimed at it",
          "[ai]") {
    Scene scene;
    Brain& warrior = scene.add({40.0F, 41.2F, 0.0F}, 180.0F, BrainType::Warrior);
    warrior.setTarget(&scene.playerBrain());
    scene.run(2);
    // The player idle: nothing to counter.
    CHECK_FALSE(coney::ai::counterTest(warrior));
    // The player starts a grab (circle tapped); while its intro plays and it aims at the AI, the test passes.
    scene.playerBrain().setTarget(&warrior);
    scene.player().record().command = command::kCircleTapped;
    bool sawGrab = false;
    for (int k = 0; k < 20; ++k) {
        scene.step.update(scene.mesh.get());
        scene.player().record().command = command::kNone;
        sawGrab = sawGrab || inGrabIntro(scene.player());
        CHECK(coney::ai::counterTest(warrior) == inGrabIntro(scene.player()));
    }
    CHECK(sawGrab);
    // A counter chance of 0 never counters.
    coney::human::FighterProfile profile = warrior.human().fighterProfile();
    profile.powerClass.counterChance = 0.0F;
    warrior.human().setFighterProfile(profile);
    CHECK_FALSE(coney::ai::counterTest(warrior));
}

TEST_CASE("only a type-3 brain facing the grabber may counter", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.2F, 0.0F}, 180.0F, BrainType::Gang);
    Brain& turned = scene.add({39.0F, 40.0F, 0.0F}, 90.0F, BrainType::Warrior);
    gang.setTarget(&scene.playerBrain());
    turned.setTarget(&scene.playerBrain());
    scene.playerBrain().setTarget(&gang);
    scene.run(2);
    scene.player().record().command = command::kCircleTapped;
    scene.step.update(scene.mesh.get());
    scene.player().record().command = command::kNone;
    REQUIRE(inGrabIntro(scene.player()));
    // A gang soldier (type 2) never counters, and a Warrior the player does not aim at does not either.
    CHECK_FALSE(coney::ai::counterTest(gang));
    CHECK_FALSE(coney::ai::counterTest(turned));
    // The face-to-face test alone: the player faces +y, a human 1.2 m ahead facing it passes, one beside it fails.
    CHECK(coney::combat::faceToFace({40.0F, 40.0F, 0.0F}, 0.0F, {40.0F, 41.2F, 0.0F}, std::numbers::pi_v<float>));
    CHECK_FALSE(
        coney::combat::faceToFace({40.0F, 40.0F, 0.0F}, 0.0F, {38.8F, 40.0F, 0.0F}, -std::numbers::pi_v<float> / 2));
}

TEST_CASE("an AI's counter in the block time answers the player's grab with 76, the player playing 77", "[ai]") {
    Scene scene;
    Brain& warrior = scene.add({40.0F, 41.2F, 0.0F}, 180.0F, BrainType::Warrior);
    coney::human::FighterProfile profile = warrior.human().fighterProfile();
    profile.powerClass.counterChance = 1.0F; // rand100 is always under 100
    warrior.human().setFighterProfile(profile);
    warrior.setTarget(&scene.playerBrain());
    scene.playerBrain().setTarget(&warrior);
    scene.run(2);
    REQUIRE(warrior.pushGoal(std::make_unique<coney::ai::BlockGoal>()));
    const int health = scene.player().fighter().health().value();
    scene.run(3);
    scene.player().record().command = command::kCircleTapped;
    scene.step.update(scene.mesh.get());
    scene.player().record().command = command::kNone;
    REQUIRE(inGrabIntro(scene.player()));
    // The AI presses R1 (command 3) in the intro; the player's next update plays the pair.
    bool countered = false;
    for (int k = 0; k < 6 && !countered; ++k) {
        scene.run(1);
        countered = warrior.human().animator().animId() == coney::combat::kAiGrabCounter;
    }
    REQUIRE(countered);
    CHECK(scene.player().animator().animId() == coney::combat::kAiGrabCounter + 1);
    // Never held: the grab ended with the counter. The player takes 76's damage (30) and is stunned.
    CHECK_FALSE(warrior.human().fighter().holdState().has_value());
    CHECK(scene.player().fighter().pairStage() == coney::human::PairStage::None);
    scene.run(1);
    CHECK(scene.player().fighter().health().value() == health - 30);
    CHECK(scene.player().fighter().victim().stunned());
}

TEST_CASE("a block extended while the target attacks punishes in its last second", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.2F, 0.0F}, 180.0F, BrainType::Gang);
    gang.setTarget(&scene.playerBrain());
    scene.playerBrain().setTarget(&gang);
    // Reactions off before the goal (so they stay off): the player's hit does not stun the AI out of its goal.
    gang.human().fighter().setHitReactionsOff(true);
    // The block runs over a fight, as a fight pushes it: the player's hit then only keeps him as the target.
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::FightGoal>()));
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::BlockGoal>()));
    scene.run(1);
    const auto* block = dynamic_cast<const coney::ai::BlockGoal*>(gang.topGoal());
    REQUIRE(block != nullptr);
    // The player presses square two updates before the block time runs out, so it is winding up at that update.
    const std::uint64_t firstUntil = block->untilMs();
    while (scene.brains.nowMs() + 3 * 1000 / 30 <= firstUntil) {
        scene.run(1);
    }
    scene.player().record().command = command::kSquarePressed;
    scene.run(1);
    scene.player().record().command = command::kNone;
    while (scene.brains.nowMs() <= firstUntil + 1000 / 30) {
        scene.run(1);
        CHECK(gang.human().record().command == command::kR1Held);
    }
    // Extended: no counter roll from now on, and in the new block time's last second a punishing attack is queued.
    REQUIRE(gang.topGoal() == block);
    CHECK(block->patterned());
    CHECK(block->untilMs() > firstUntil);
    const std::uint64_t lastSecond = block->untilMs() - coney::ai::kBlockLastMs;
    while (block->active()) {
        CHECK(gang.actionCount() == 0);
        scene.run(1);
    }
    CHECK(scene.brains.nowMs() >= lastSecond);
    // The punishing attack pressed at once: one of Att_Normal's kinds in the punishing set that can start (31 or 42,
    // square; the tackle 21; the grab 22).
    coney::combat::CombatRandom random(1);
    const coney::combat::CommandId pressed = gang.human().record().command;
    CHECK((pressed == command::kSquarePressed || pressed == coney::ai::commandOf(21, random) ||
           pressed == coney::ai::commandOf(22, random)));
}

TEST_CASE("a reaction goal comes and goes one update after the state that calls for it", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.0F, 0.0F}, 180.0F, BrainType::Gang);
    Human& ai = gang.human();
    scene.run(2);
    // A stunning hit from the player standing in front.
    ai.hit(strikeFromFront(coney::combat::kRangeFlagStun));
    bool heldBefore = false;
    bool sawReaction = false;
    for (int k = 0; k < 150; ++k) {
        scene.step.update(scene.mesh.get());
        // The reaction goal after this step's brains is the state at the end of the step before.
        CHECK((gang.reactionGoal() != nullptr) == heldBefore);
        sawReaction = sawReaction || gang.reactionGoal() != nullptr;
        heldBefore = coney::ai::reactionGoalFor(ai) != nullptr;
    }
    CHECK(sawReaction);
    CHECK_FALSE(heldBefore);
}
