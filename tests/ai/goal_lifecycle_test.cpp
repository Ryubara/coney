// SPDX-License-Identifier: GPL-3.0-or-later
// The brain's shared goal and action lifecycle (docs/research/ai/index.md#goals, #actions, #update-goals and
// #reaction-goals): when a goal is started, resumed, processed, ended and freed, when an action is started, aborted and
// freed, how the reaction goal takes over from the stack and hands it back, and the order of a tactic's end. Synthetic
// goals and actions that log their hooks; game time is the steps run (1/30 s each).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/action.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/hub_goals.h"
#include "ai/idle_goals.h"
#include "ai/tactic.h"
#include "combat/reactions.h"
#include "human/human.h"
#include "support/ai_fixtures.h"

using coney::ai::ActionStatus;
using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::GoalStatus;
using coney::ai::GoalType;
using coney::test::AiScene;

namespace {

// One step of game time, ms, as Brains counts it.
std::uint64_t stepMs(std::uint64_t step) { return step * 1000 / 30; }

// A goal that logs its hooks into `log` and returns `status` from process(); `timeLimitMs` is the base's time limit.
class LogGoal final : public coney::ai::Goal {
  public:
    LogGoal(std::string name, std::vector<std::string>& log, int timeLimitMs = coney::ai::kNoGoalTimeLimit)
        : Goal(GoalType::Fight, timeLimitMs), m_name(std::move(name)), m_log(&log) {}
    void start(Brain& /*brain*/) override { m_log->push_back(m_name + " start"); }
    void resume(Brain& /*brain*/) override { m_log->push_back(m_name + " resume"); }
    void suspend(Brain& /*brain*/) override { m_log->push_back(m_name + " suspend"); }
    void end(Brain& /*brain*/) override { m_log->push_back(m_name + " end"); }
    GoalStatus process(Brain& /*brain*/) override {
        m_log->push_back(m_name + " process");
        return status;
    }
    GoalStatus status = GoalStatus::Stop;

  private:
    std::string m_name;
    std::vector<std::string>* m_log;
};

// An action that logs its start, updates and aborts into `log`: its start may say done at once, it is done after
// `updates` updates, and it may refuse to abort.
class LogAction final : public coney::ai::Action {
  public:
    LogAction(std::string name, std::vector<std::string>& log, int updates = 1000, bool startsDone = false,
              bool refuses = false)
        : m_name(std::move(name)), m_log(&log), m_updates(updates), m_startsDone(startsDone), m_refuses(refuses) {}
    ActionStatus start(Brain& /*brain*/) override {
        m_log->push_back(m_name + " start");
        return m_startsDone ? ActionStatus::Done : ActionStatus::Running;
    }
    ActionStatus update(Brain& /*brain*/) override {
        m_log->push_back(m_name + " update");
        return --m_updates <= 0 ? ActionStatus::Done : ActionStatus::Running;
    }
    bool abort(Brain& /*brain*/) override {
        m_log->push_back(m_name + " abort");
        return !m_refuses;
    }

  private:
    std::string m_name;
    std::vector<std::string>* m_log;
    int m_updates;
    bool m_startsDone;
    bool m_refuses;
};

// A 30-damage S1 from the player standing at (40, 40), with the range flags `flags`.
coney::human::IncomingHit strikeFromPlayer(std::uint16_t flags) {
    coney::human::IncomingHit hit;
    hit.damage = 30;
    hit.attackAnim = 12;
    hit.code = 0x0a;
    hit.flags = flags;
    hit.attacker = coney::anim::Vec3{40.0F, 40.0F, 0.0F};
    return hit;
}

// Stuns `brain`'s human from the front and steps the scene `steps` times; returns whether a reaction goal was seen
// after any step.
bool stunAndWatch(AiScene& scene, Brain& brain, int steps) {
    scene.run(2);
    brain.human().hit(strikeFromPlayer(coney::combat::kRangeFlagStun));
    bool seen = false;
    for (int k = 0; k < steps; ++k) {
        scene.step.update(scene.mesh.get());
        seen = seen || brain.reactionGoal() != nullptr;
    }
    return seen;
}

// A gang tactic that records, as its class End runs, how many goals each member has.
class EndWatchingTactic final : public coney::ai::Tactic {
  public:
    EndWatchingTactic() : Tactic(0x20, "") {}
    void end(coney::ai::Gang& gang) override {
        for (const Brain* member : gang.members()) {
            goalsAtEnd.push_back(member->goalCount());
        }
    }
    std::vector<std::size_t> goalsAtEnd;
};

} // namespace

TEST_CASE("a pop that empties the goal stack sends the human event 0xd", "[ai][lifecycle]") {
    // Expected: ai/index.md#update-goals, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    brain.setServices(&scene.services);
    std::vector<std::string> log;
    REQUIRE(brain.pushGoal(std::make_unique<LogGoal>("low", log)));
    REQUIRE(brain.pushGoal(std::make_unique<LogGoal>("high", log)));
    // A pop that leaves a goal sends nothing.
    brain.popGoal();
    CHECK(scene.services.humanEvents.empty());
    // The last pop does: event 0xd about the human himself (+0x24).
    brain.popGoal();
    REQUIRE(scene.services.humanEvents.size() == 1);
    CHECK(scene.services.humanEvents.front().first == &brain);
    CHECK(scene.services.humanEvents.front().second.id == 0x0d);
    CHECK(scene.services.humanEvents.front().second.victim == &brain);
}

TEST_CASE("a pop resumes the new top at once when the action queue is empty", "[ai][lifecycle]") {
    // Expected: ai/index.md#update-goals, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    std::vector<std::string> log;
    REQUIRE(brain.pushGoal(std::make_unique<LogGoal>("low", log)));
    brain.update(stepMs(1));
    REQUIRE(brain.pushGoal(std::make_unique<LogGoal>("high", log)));
    brain.update(stepMs(2));
    log.clear();
    brain.popGoal();
    // Resumed in the pop, not before its next process.
    CHECK(log == std::vector<std::string>{"high end", "low resume"});
}

TEST_CASE("a goal popped before it started is freed without its End", "[ai][lifecycle]") {
    // Expected: ai/index.md#goals, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    std::vector<std::string> log;
    REQUIRE(brain.pushGoal(std::make_unique<LogGoal>("never", log)));
    brain.popGoal();
    CHECK(log.empty());
    CHECK(brain.goalCount() == 0);
}

TEST_CASE("clearing the goals resets the goal base", "[ai][lifecycle]") {
    // Expected: ai/index.md#update-goals, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    std::vector<std::string> log;
    REQUIRE(brain.pushGoal(std::make_unique<LogGoal>("own", log)));
    brain.markGoalBase();
    REQUIRE(brain.goalBase() == 0);
    brain.clearGoals();
    CHECK(brain.goalBase() == -1);
}

TEST_CASE("clearing the actions frees an unstarted action that would refuse to abort", "[ai][lifecycle]") {
    // Expected: ai/index.md#actions, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    std::vector<std::string> log;
    REQUIRE(brain.queueAction(std::make_unique<LogAction>("busy", log, 1000, false, true)));
    // Never started, so it is not asked: it is freed.
    CHECK(brain.clearActions());
    CHECK(brain.actionCount() == 0);
    CHECK(log.empty());
}

TEST_CASE("an action whose start says done is freed without its abort", "[ai][lifecycle]") {
    // Expected: ai/index.md#actions, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    std::vector<std::string> log;
    REQUIRE(brain.queueAction(std::make_unique<LogAction>("quick", log, 1000, true)));
    brain.update(stepMs(1));
    CHECK(log == std::vector<std::string>{"quick start"});
    CHECK(brain.actionCount() == 0);
}

TEST_CASE("actions done at once run one after another in the same update", "[ai][lifecycle]") {
    // Expected: ai/index.md#update-goals, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    std::vector<std::string> log;
    REQUIRE(brain.queueAction(std::make_unique<LogAction>("a", log, 1)));
    REQUIRE(brain.queueAction(std::make_unique<LogAction>("b", log, 1)));
    REQUIRE(brain.queueAction(std::make_unique<LogAction>("c", log, 2)));
    brain.update(stepMs(1));
    // a and b finish (each aborted, having started, then freed); c runs on.
    CHECK(log == std::vector<std::string>{"a start", "a update", "a abort", "b start", "b update", "b abort", "c start",
                                          "c update"});
    CHECK(brain.actionCount() == 1);
}

TEST_CASE("a brain made late in a match starts its first action on its first update", "[ai][lifecycle]") {
    // Expected: ai/index.md#actions, confirmed (code): Brain_Init sets the last update time to the clock, so the first
    // update's 16-bit elapsed time is one step, not the whole match.
    AiScene scene;
    // 1100 steps: 36,666 ms, past what 16 bits hold signed.
    scene.run(1100);
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    std::vector<std::string> log;
    REQUIRE(brain.queueAction(std::make_unique<LogAction>("first", log)));
    scene.run(1);
    CHECK(log == std::vector<std::string>{"first start", "first update"});
}

TEST_CASE("a goal past its time limit is done without its process, and popped", "[ai][lifecycle]") {
    // Expected: ai/index.md#goals, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    std::vector<std::string> log;
    REQUIRE(brain.pushGoal(std::make_unique<LogGoal>("timed", log, 100)));
    // Started at 33 ms, so its limit is 133 ms: processed through 133, done at 166.
    for (std::uint64_t step = 1; step <= 5; ++step) {
        brain.update(stepMs(step));
    }
    CHECK(log == std::vector<std::string>{"timed start", "timed process", "timed process", "timed process",
                                          "timed process", "timed end"});
    CHECK(brain.goalCount() == 0);
}

TEST_CASE("a new reaction goal suspends the top and starts on the next update; ended, the stack runs that update",
          "[ai][lifecycle]") {
    // Expected: ai/index.md#reaction-goals, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({40.0F, 41.0F, 0.0F}, 180.0F);
    // Never fights back, so the hit pushes no fight goals over the logging one.
    brain.setThreatResponse(0);
    std::vector<std::string> log;
    REQUIRE(brain.pushGoal(std::make_unique<LogGoal>("g", log)));
    scene.run(2);
    brain.human().hit(strikeFromPlayer(coney::combat::kRangeFlagStun));
    bool made = false;
    bool ended = false;
    for (int k = 0; k < 150 && !ended; ++k) {
        log.clear();
        const bool before = brain.reactionGoal() != nullptr;
        scene.step.update(scene.mesh.get());
        const bool after = brain.reactionGoal() != nullptr;
        if (!before && after) {
            // Made this update: the top suspended, the reaction goal not yet started, the stack not processed.
            made = true;
            CHECK(log == std::vector<std::string>{"g suspend"});
            CHECK_FALSE(brain.reactionGoal()->started());
        }
        if (before && !after) {
            // Ended this update: the top resumed and processed in the same update.
            ended = true;
            CHECK(log == std::vector<std::string>{"g resume", "g process"});
        }
    }
    CHECK(made);
    CHECK(ended);
}

TEST_CASE("a calm civilian or dealer makes no reaction goal", "[ai][lifecycle]") {
    // Expected: ai/index.md#reaction-goals, confirmed (code)
    for (const BrainType type : {BrainType::Civilian, BrainType::Dealer}) {
        AiScene scene;
        Brain& brain = scene.add({40.0F, 41.0F, 0.0F}, 180.0F, type);
        CHECK_FALSE(stunAndWatch(scene, brain, 30));
    }
}

TEST_CASE("the character classes the reaction goal leaves alone make none when stunned", "[ai][lifecycle]") {
    // Expected: ai/index.md#reaction-goals, confirmed (code)
    for (const int characterClass : {0x77, 0x78, 0x4f, 0x80, 0xb2, 0x87, 0x62}) {
        AiScene scene;
        Brain& brain = scene.add({40.0F, 41.0F, 0.0F}, 180.0F);
        brain.setCharacterClass(characterClass);
        INFO("class " << characterClass);
        CHECK_FALSE(stunAndWatch(scene, brain, 30));
    }
}

TEST_CASE("a grab goal still walking to its target holds the reaction goal off", "[ai][lifecycle]") {
    // Expected: ai/index.md#reaction-goals, confirmed (code)
    AiScene scene;
    Brain& brain = scene.add({40.0F, 41.0F, 0.0F}, 180.0F);
    const coney::ai::HubGoalServices services;
    REQUIRE(brain.pushGoal(std::make_unique<coney::ai::GrabTargetGoal>(1.0, services)));
    // An action that never ends keeps the grab goal from starting, so it stays on the stack, not holding.
    std::vector<std::string> log;
    REQUIRE(brain.queueAction(std::make_unique<LogAction>("wait", log)));
    CHECK_FALSE(stunAndWatch(scene, brain, 30));
}

TEST_CASE("a tactic's end pops its members to their goal bases before its class End runs", "[ai][tactics][lifecycle]") {
    // Expected: ai-code.md#t1-tactic-core, confirmed (code)
    AiScene scene;
    const int gang = scene.brains.gangs().create(19, "Crew");
    Brain& member = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(gang, member);
    REQUIRE(member.pushGoal(std::make_unique<coney::ai::IdleGoal>()));
    auto owned = std::make_unique<EndWatchingTactic>();
    const EndWatchingTactic& tactic = *owned;
    scene.brains.gangs().setTactic(gang, std::move(owned));
    scene.run(1);
    REQUIRE(member.goalBase() == 0);
    REQUIRE(member.pushGoal(std::make_unique<coney::ai::IdleGoal>()));
    scene.brains.gangs().setTactic(gang, nullptr);
    // The goal above the base was gone when the class End ran.
    CHECK(tactic.goalsAtEnd == std::vector<std::size_t>{1});
}
