// SPDX-License-Identifier: GPL-3.0-or-later
// TacticVandalize and its Destroy goal (docs/research/ai-code.md#t2-vandalize, docs/research/ai-goals.md#goal-destroy):
// the looters wreck their object zone and call back 1 once it is empty, 6 only when attacked, 5 when hurt. Synthetic
// floor; the level's objects are a fake (ItemServices).
#include "ai/tactic_vandalize.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/destroy_goal.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/smash_goals.h"
#include "scripting/story_bindings.h"
#include "support/ai_fixtures.h"
#include "support/fake_items.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::anim::Vec3;
using coney::test::AiScene;
using coney::test::FakeItems;

namespace {

// The callback level34's intro looters are given (`P1.DoneLooting`, level34_riot.lua P1.StartIntroLooters).
const std::string kCallback = "DoneLooting";

// `TacticVandalize(gang, zone, delay, callback, -1)` as level34_riot.lua's P1.StartIntroLooters calls it (zone 51,
// 1000 ms there; the fake's zone here).
coney::script::TacticCall vandalizeCall(int gang, std::uint32_t delayMs = 1000, float range = -1.0F) {
    coney::script::TacticCall call;
    call.gang = gang;
    call.kind = coney::script::TacticKind::Vandalize;
    call.zone = static_cast<std::uint32_t>(coney::test::kZone);
    call.delayMs = delayMs;
    call.callback = kCallback;
    call.range = range;
    return call;
}

// The codes the tactic called back with, in order.
std::vector<int> codes(const AiScene& scene) {
    std::vector<int> found;
    for (const coney::test::ScriptCall& call : scene.services.calls) {
        if (call.function == kCallback && call.args.size() == 2) {
            found.push_back(static_cast<int>(call.args[1]));
        }
    }
    return found;
}

// Two looters in a gang of their own with the wreck tactic over `items`; returns the gang id.
int startLooters(AiScene& scene, FakeItems& items, const std::vector<Brain*>& looters, std::uint32_t delayMs = 1000,
                 float range = -1.0F) {
    auto& gangs = scene.brains.gangs();
    const int gang = gangs.create(23, "IntroLooters");
    for (Brain* looter : looters) {
        gangs.addMember(gang, *looter);
    }
    const coney::ai::TacticServices services{.scripts = &scene.services, .items = &items};
    gangs.setTactic(gang, std::make_unique<coney::ai::VandalizeTactic>(vandalizeCall(gang, delayMs, range), services));
    return gang;
}

// The tactic of gang `gang`.
coney::ai::Tactic* tacticOf(AiScene& scene, int gang) {
    coney::ai::Gang* found = scene.brains.gangs().find(gang);
    return found != nullptr ? found->tactic() : nullptr;
}

} // namespace

TEST_CASE("TacticVandalize does not finish while its zone has objects, and answers 1 once it is empty",
          "[ai][vandalize]") {
    // Expected: ai-code.md#t2-vandalize, confirmed (code): VandalizeTactic_Process (0x0031ea00) returns 1 only when
    // ObjZone_HasObjects (0x0039a580) finds nothing left, checked every 3 s; codes 5 and 6 come only from events 1
    // and 16 (VandalizeTactic_OnEvent 0x0031eaf0).
    AiScene scene;
    Brain& first = scene.add(Vec3{20.0F, 20.0F, 0.0F}, 0.0F);
    Brain& second = scene.add(Vec3{22.0F, 20.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.add(600, Vec3{20.0F, 30.0F, 0.0F});
    items.add(601, Vec3{24.0F, 30.0F, 0.0F});
    const int gang = startLooters(scene, items, {&first, &second});

    // 20 s with the objects whole and nobody attacking: no code at all.
    scene.run(60 * 20);
    CHECK(codes(scene).empty());
    CHECK(first.findGoal(GoalType::Destroy) != nullptr);
    CHECK(second.findGoal(GoalType::Destroy) != nullptr);

    // Both broken: 1 within the next check.
    items.smashed = {600, 601};
    scene.run(60 * 4);
    REQUIRE_FALSE(codes(scene).empty());
    CHECK(codes(scene).front() == coney::ai::kTacTimeUp);
    CHECK(tacticOf(scene, gang) != nullptr);
}

TEST_CASE("TacticVandalize answers 6 when a looter is attacked and 5 when he is hurt", "[ai][vandalize]") {
    // Expected: ai-code.md#t2-vandalize, confirmed (code): VandalizeTactic_OnEvent (0x0031eaf0) fires 6 for event
    // 16 and 5 for event 1 (3 for event 11), after VandalizeTactic_OnDamage (0x0031e620) has the hurt looter fight
    // back: down to his Destroy goal, the attacker an enemy, a Melee goal of 8 s and a fight goal of 4 s on top.
    AiScene scene;
    Brain& looter = scene.add(Vec3{20.0F, 20.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.add(600, Vec3{20.0F, 30.0F, 0.0F});
    const int gang = startLooters(scene, items, {&looter});
    scene.run(2);
    coney::ai::Tactic* tactic = tacticOf(scene, gang);
    coney::ai::Gang* looters = scene.brains.gangs().find(gang);
    REQUIRE(tactic != nullptr);
    REQUIRE(looters != nullptr);

    static_cast<void>(tactic->event(
        *looters, looter, coney::ai::BrainEvent{.id = coney::ai::kEventAttackWarning, .other = &scene.player()}));
    CHECK(codes(scene) == std::vector<int>{coney::ai::kTacAttacked});

    static_cast<void>(tactic->event(*looters, looter,
                                    coney::ai::BrainEvent{.id = coney::ai::kEventDamaged, .other = &scene.player()}));
    // The attacker made an enemy may also fire 3 first (event 11, an enemy added, which the tactic hears too); then 5.
    const std::vector<int> after = codes(scene);
    REQUIRE(after.size() >= 2);
    CHECK(after.back() == coney::ai::kTacDamage);
    CHECK(std::ranges::all_of(after.begin() + 1, after.end() - 1,
                              [](int code) { return code == coney::ai::kTacSeeEnemy; }));
    REQUIRE(looter.topGoal() != nullptr);
    CHECK(looter.topGoal()->type() == GoalType::Fight);
    CHECK(looter.findGoal(GoalType::Melee) != nullptr);
    CHECK(looter.findGoal(GoalType::Destroy) != nullptr);
    CHECK(std::ranges::contains(looter.enemies(), &scene.player()));
}

TEST_CASE("a looter waits the tactic's delay, then walks up to the zone's nearest object and smashes it",
          "[ai][vandalize]") {
    // Expected: ai-goals.md#goal-destroy, confirmed (code): DestroyGoal_Process (0x002cdbc0) waits the delay to the
    // delay + 100 ms, picks the zone's nearest object (DestroyGoal_PickTarget 0x002cd698, ObjZone_FindNearestObject
    // 0x0039a2c0) and pushes DestroyItem (0x002dd0c8) for one that is not carried.
    AiScene scene;
    scene.brains.setCollision(scene.mesh.get());
    Brain& looter = scene.add(Vec3{20.0F, 20.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.add(600, Vec3{20.0F, 26.0F, 0.0F});
    items.add(601, Vec3{20.0F, 40.0F, 0.0F});
    static_cast<void>(startLooters(scene, items, {&looter}));

    // Within the delay he has no target yet.
    scene.run(30);
    const auto* destroy = dynamic_cast<const coney::ai::DestroyGoal*>(looter.findGoal(GoalType::Destroy));
    REQUIRE(destroy != nullptr);
    CHECK_FALSE(destroy->target().has_value());

    // After it, the nearer object, and a DestroyItem that reaches it and swings.
    const coney::ai::DestroyItemGoal* smash = nullptr;
    for (int k = 0; k < 60 * 3 && smash == nullptr; ++k) {
        scene.run(1);
        smash = dynamic_cast<const coney::ai::DestroyItemGoal*>(looter.findGoal(GoalType::DestroyItem));
    }
    REQUIRE(smash != nullptr);
    CHECK(smash->object() == 600.0);
    for (int k = 0; k < 60 * 10 && !smash->swung(); ++k) {
        scene.run(1);
        smash = dynamic_cast<const coney::ai::DestroyItemGoal*>(looter.findGoal(GoalType::DestroyItem));
        if (smash == nullptr) {
            break;
        }
    }
    CHECK((smash == nullptr || smash->swung()));

    // Once it is broken he goes on to the other one.
    items.smashed = {600};
    smash = nullptr;
    for (int k = 0; k < 60 * 5 && smash == nullptr; ++k) {
        scene.run(1);
        const auto* next = dynamic_cast<const coney::ai::DestroyItemGoal*>(looter.findGoal(GoalType::DestroyItem));
        if (next != nullptr && next->object() == 601.0) {
            smash = next;
        }
    }
    CHECK(smash != nullptr);
}

TEST_CASE("a looter fetches a carried object of the zone, and leaves one beyond the leader's range",
          "[ai][vandalize]") {
    // Expected: ai-goals.md#goal-destroy, confirmed (code): DestroyGoal_Process (0x002cdbc0) sends a carried object
    // (anim set 4 or 5) to GetItem, gait 4; DestroyGoal_PickTarget (0x002cd698) drops a target farther than the
    // range from the gang's leader.
    AiScene scene;
    Brain& looter = scene.add(Vec3{20.0F, 20.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.carriedSet = 4;
    items.add(600, Vec3{20.0F, 24.0F, 0.0F});
    static_cast<void>(startLooters(scene, items, {&looter}, 500));
    bool fetched = false;
    for (int k = 0; k < 60 * 3 && !fetched; ++k) {
        scene.run(1);
        fetched = looter.findGoal(GoalType::GetItem) != nullptr;
    }
    CHECK(fetched);

    AiScene far;
    Brain& alone = far.add(Vec3{20.0F, 20.0F, 0.0F}, 0.0F);
    FakeItems distant;
    distant.add(700, Vec3{20.0F, 60.0F, 0.0F});
    static_cast<void>(startLooters(far, distant, {&alone}, 500, 10.0F));
    far.run(60 * 5);
    const auto* destroy = dynamic_cast<const coney::ai::DestroyGoal*>(alone.findGoal(GoalType::Destroy));
    REQUIRE(destroy != nullptr);
    CHECK_FALSE(destroy->target().has_value());
    CHECK(alone.findGoal(GoalType::DestroyItem) == nullptr);
}
