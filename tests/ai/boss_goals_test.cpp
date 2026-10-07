// SPDX-License-Identifier: GPL-3.0-or-later
// The Diego and Vargas fight (docs/research/ai.md#boss-diego-vargas): the tired goal stuns the boss and lets him up
// when his fatigue runs out; six hits in the fight tire a BigBrawler; and TacticBossScenarioA gives each Hurricane his
// goal, holds Diego's health above the stage's cap until he is tired, then answers 18 for the break and 1 once it is
// done. Synthetic scenes.
#include "ai/boss_goals.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/story_tactics.h"
#include "ai/tactic_boss.h"
#include "human/fighter.h"
#include "human/human.h"
#include "scripting/story_bindings.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// The callback codes the scene's services were called with for `function`, in order.
std::vector<int> codesOf(const AiScene& scene, const char* function) {
    std::vector<int> codes;
    for (const coney::test::ScriptCall& call : scene.services.calls) {
        if (call.function == function && call.args.size() >= 2) {
            codes.push_back(static_cast<int>(call.args[1]));
        }
    }
    return codes;
}

} // namespace

TEST_CASE("a tired boss stands stunned until his fatigue runs out, then gets up with the rage clip", "[ai][boss]") {
    AiScene scene;
    Brain& boss = scene.add(Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    REQUIRE(boss.pushGoal(std::make_unique<coney::ai::TiredGoal>(1000, 30, &scene.services)));
    scene.run(3);
    CHECK(boss.human().fighter().victim().stunned());
    CHECK(boss.topGoal()->type() == GoalType::Tired);
    scene.run(60); // 2 s: past the second of fatigue
    CHECK_FALSE(boss.human().fighter().victim().stunned());
    CHECK(std::ranges::find(scene.services.clips, coney::ai::boss_anim::kRage) != scene.services.clips.end());
    CHECK((boss.topGoal() == nullptr || boss.topGoal()->type() != GoalType::Tired));
}

TEST_CASE("six hits in the fight tire a BigBrawler for his stage's fatigue", "[ai][boss]") {
    AiScene scene;
    Brain& boss = scene.add(Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::BigBrawlerOrder order;
    order.tables.fatigue = {7, 4, 3};
    REQUIRE(boss.pushGoal(std::make_unique<coney::ai::BigBrawlerGoal>(order, &scene.services, nullptr)));
    auto* brawler = static_cast<coney::ai::BigBrawlerGoal*>(boss.findGoal(GoalType::BigBrawler));
    REQUIRE(brawler != nullptr);
    CHECK(brawler->fatigueMs() == 7000);
    CHECK(brawler->damagePercent() == 30);
    // Stage 1 opens with the taunt, then fights.
    scene.run(2);
    REQUIRE(brawler->state() == coney::ai::BigBrawlerGoal::State::Fight);
    for (int hit = 0; hit < 5; ++hit) {
        brawler->onHit(boss);
    }
    CHECK(brawler->hits() == 5);
    CHECK(boss.topGoal()->type() == GoalType::BigBrawler);
    brawler->onHit(boss);
    CHECK(brawler->hits() == 0);
    CHECK(boss.topGoal()->type() == GoalType::Tired);
}

TEST_CASE("TacticBossScenarioA caps Diego's health until he is tired, then answers 18 and 1 for the break",
          "[ai][boss]") {
    AiScene scene;
    const int gang = scene.brains.gangs().create(19, "Hurricanes");
    Brain& diego = scene.add(Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    diego.setCharacterClass(coney::ai::kDiegoClass);
    scene.brains.gangs().addMember(gang, diego);
    coney::script::TacticCall call;
    call.kind = coney::script::TacticKind::BossDiegoVargas;
    call.callback = "P1.Boss";
    call.boss.stage = 1;
    const coney::ai::TacticServices services{.scripts = &scene.services};
    scene.brains.gangs().setTactic(gang, coney::ai::makeStoryTactic(call, {}, services));
    scene.run(2);
    REQUIRE(diego.findGoal(GoalType::BigBrawler) != nullptr);

    // Below the stage-1 cap while not tired: put back one point above it.
    diego.human().setHealthPercent(50.0F);
    scene.run(1);
    CHECK(diego.human().healthPercent() == Catch::Approx(67.0F).margin(0.5F));
    CHECK(codesOf(scene, "P1.Boss").empty());

    // Tired and at the cap: the break starts (18), and once its hold has played Diego's stage is over (1).
    REQUIRE(diego.pushGoal(std::make_unique<coney::ai::TiredGoal>(5000, 30, &scene.services)));
    diego.human().setHealthPercent(60.0F);
    scene.run(2);
    CHECK(codesOf(scene, "P1.Boss") == std::vector<int>{coney::ai::kTacAnimStart});
    CHECK(diego.human().healthPercent() == Catch::Approx(66.0F).margin(0.5F));
    CHECK(std::ranges::find(scene.services.clips, coney::ai::boss_anim::kBreak) != scene.services.clips.end());
    scene.run(90);
    CHECK(codesOf(scene, "P1.Boss") == std::vector<int>{coney::ai::kTacAnimStart, 1});
}
