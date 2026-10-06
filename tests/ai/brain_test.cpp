// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brain.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/attack_action.h"
#include "ai/block_goal.h"
#include "ai/brains.h"
#include "ai/goal.h"
#include "ai/reaction_goals.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/reactions.h"
#include "human/human.h"
#include "human/humans.h"
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

TEST_CASE("the weighted pick draws each playable kind by its share of the weights", "[ai]") {
    coney::ai::AttackWeights weights{};
    weights[0] = 10;   // X1
    weights[1] = 30;   // S1
    weights[22] = 200; // a grab between humans: not playable, never drawn
    coney::combat::CombatRandom random(7);
    int x1 = 0;
    constexpr int kDraws = 8000;
    for (int k = 0; k < kDraws; ++k) {
        const int kind = coney::ai::pickAttack(weights, random).value_or(-1);
        REQUIRE((kind == 0 || kind == 1));
        x1 += kind == 0 ? 1 : 0;
    }
    CHECK(x1 > kDraws * 22 / 100);
    CHECK(x1 < kDraws * 28 / 100);
    // The same seed draws the same kinds.
    coney::combat::CombatRandom first(3);
    coney::combat::CombatRandom second(3);
    for (int k = 0; k < 50; ++k) {
        CHECK(coney::ai::pickAttack(weights, first) == coney::ai::pickAttack(weights, second));
    }
    CHECK_FALSE(coney::ai::pickAttack(coney::ai::AttackWeights{}, random).has_value());
}

TEST_CASE("a Warrior waits the attack delay times its class's factor, halved when its target targets it", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({41.0F, 40.0F, 0.0F}, 270.0F, BrainType::Gang);
    // 200 ms × 20 = 4 s; a kind of 1000 ms × 20; halved, 2 s.
    CHECK(gang.attackDelayMs(1, false, false) == 4000);
    CHECK(gang.attackDelayMs(20, false, false) == 20000);
    CHECK(gang.attackDelayMs(1, false, true) == 2000);

    // The attack action: the next attack is now + the delay, the target's is now + the kind's own delay.
    gang.setTarget(&scene.playerBrain());
    REQUIRE(gang.queueAction(std::make_unique<coney::ai::AttackAction>(1, 0)));
    gang.update(stepMs(30));
    CHECK(gang.nextAttackMs() == stepMs(30) + 4000);
    CHECK(scene.playerBrain().attackableAtMs() == stepMs(30) + 200);
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

TEST_CASE("the counter test passes only against a target in a grab's or a tackle's intro that aims at the human",
          "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.2F, 0.0F}, 180.0F, BrainType::Gang);
    gang.setTarget(&scene.playerBrain());
    scene.run(2);
    // The player idle: nothing to counter.
    CHECK_FALSE(coney::ai::counterTest(gang));
    // The player starts a grab (circle tapped); while its intro plays and it aims at the AI, the test passes.
    scene.playerBrain().setTarget(&gang);
    scene.player().record().command = command::kCircleTapped;
    bool sawGrab = false;
    for (int k = 0; k < 20; ++k) {
        scene.step.update(scene.mesh.get());
        scene.player().record().command = command::kNone;
        sawGrab = sawGrab || inGrabIntro(scene.player());
        CHECK(coney::ai::counterTest(gang) == inGrabIntro(scene.player()));
    }
    CHECK(sawGrab);
    // A counter chance of 0 never counters.
    coney::human::FighterProfile profile = gang.human().fighterProfile();
    profile.powerClass.counterChance = 0.0F;
    gang.human().setFighterProfile(profile);
    CHECK_FALSE(coney::ai::counterTest(gang));
}

TEST_CASE("the counter is rolled on every update of the block time and writes R1 pressed against a grab", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.2F, 0.0F}, 180.0F, BrainType::Gang);
    coney::human::FighterProfile profile = gang.human().fighterProfile();
    profile.powerClass.counterChance = 1.0F; // rand100 is always under 100
    gang.human().setFighterProfile(profile);
    gang.setTarget(&scene.playerBrain());
    scene.playerBrain().setTarget(&gang);
    scene.run(2);
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::BlockGoal>()));
    // Whether the player was in a grab's intro and whether the grab held the AI before the brains ran, and what the
    // AI's record holds after them.
    struct Seen {
        bool intro = false;
        bool held = false;
        coney::combat::CommandId written = command::kNone;
    };
    std::vector<Seen> seen;
    scene.step.setBrains([&scene, &seen, &gang](std::span<Human* const> /*humans*/) {
        const bool intro = inGrabIntro(scene.player());
        const bool held = gang.human().fighter().holdState().has_value();
        scene.brains.update();
        seen.push_back(Seen{.intro = intro, .held = held, .written = gang.human().record().command});
    });
    scene.run(3);
    scene.player().record().command = command::kCircleTapped;
    scene.step.update(scene.mesh.get());
    scene.player().record().command = command::kNone;
    scene.run(15);
    // Once the grab holds the AI its reaction goal holds the block off and it writes nothing (the counter 76 that its
    // R1 pressed in the intro asks for is not built, so the grab goes on).
    bool countered = false;
    bool held = false;
    for (const Seen& update : seen) {
        const coney::combat::CommandId wanted =
            update.held ? command::kNone : (update.intro ? command::kR1Pressed : command::kR1Held);
        CHECK(update.written == wanted);
        countered = countered || update.written == command::kR1Pressed;
        held = held || update.held;
    }
    CHECK(countered);
    CHECK(held);
}

TEST_CASE("a block extended while the target attacks punishes in its last second", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.2F, 0.0F}, 180.0F, BrainType::Gang);
    gang.setTarget(&scene.playerBrain());
    scene.playerBrain().setTarget(&gang);
    // Reactions off before the goal (so they stay off): the player's hit does not stun the AI out of its goal.
    gang.human().fighter().setHitReactionsOff(true);
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
    // The punishing attack pressed at once: one of Att_Normal's kinds in the punishing set (31 or 42, square).
    CHECK(gang.human().record().command == command::kSquarePressed);
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
