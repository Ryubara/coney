// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/swap_prompt.h"

#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "human/script_state.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::BrainType;

namespace {

// Player 1 at (40, 40) facing +y with a Warrior of his gang 1 m ahead of him.
struct SwapScene {
    coney::test::AiScene scene;
    Brain* warrior = nullptr;

    SwapScene() {
        auto& gangs = scene.brains.gangs();
        const int gang = gangs.create(0, "Warriors");
        gangs.addMember(gang, scene.player());
        warrior = &scene.add(coney::anim::Vec3{40.0F, 41.0F, 0.0F}, 180.0F, BrainType::Warrior);
        gangs.addMember(gang, *warrior);
    }
};

} // namespace

TEST_CASE("a Warrior in front of the player offers the swap while either holds an object", "[ai][swap]") {
    SwapScene s;
    coney::human::ScriptState& script = s.warrior->human().script();
    script.talkText = "his own";
    // Neither holds anything: not talkable, and his own text is left.
    coney::ai::thinkSwapPrompt(*s.warrior);
    CHECK_FALSE(script.talkable);
    CHECK(script.talkText == "his own");
    // The player holds one: 0xe, in place of his text.
    s.scene.player().human().script().heldObject = 7.0;
    coney::ai::thinkSwapPrompt(*s.warrior);
    CHECK(script.talkable);
    CHECK(script.talkString == coney::ai::kSwapPlayerHolds);
    CHECK(script.talkText.empty());
    // Both hold one: 0xc; he alone: 0xd.
    script.heldObject = 8.0;
    coney::ai::thinkSwapPrompt(*s.warrior);
    CHECK(script.talkString == coney::ai::kSwapBothHold);
    s.scene.player().human().script().heldObject = 0.0;
    coney::ai::thinkSwapPrompt(*s.warrior);
    CHECK(script.talkString == coney::ai::kSwapWarriorHolds);
}

TEST_CASE("the swap needs the Warrior in reach, in front of the player and in his gang", "[ai][swap]") {
    SwapScene s;
    coney::human::ScriptState& script = s.warrior->human().script();
    s.scene.player().human().script().heldObject = 7.0;
    coney::ai::thinkSwapPrompt(*s.warrior);
    REQUIRE(script.talkable);
    // Behind the player: no prompt, though the last string stays.
    s.warrior->human().place(coney::anim::Vec3{40.0F, 39.0F, 0.0F}, 0.0F);
    coney::ai::thinkSwapPrompt(*s.warrior);
    CHECK_FALSE(script.talkable);
    CHECK(script.talkString == coney::ai::kSwapPlayerHolds);
    // Out of reach ahead.
    s.warrior->human().place(coney::anim::Vec3{40.0F, 41.6F, 0.0F}, 0.0F);
    coney::ai::thinkSwapPrompt(*s.warrior);
    CHECK_FALSE(script.talkable);
    // Back in front but out of the player's gang.
    s.warrior->human().place(coney::anim::Vec3{40.0F, 41.0F, 0.0F}, 0.0F);
    s.scene.brains.gangs().removeMember(*s.warrior);
    coney::ai::thinkSwapPrompt(*s.warrior);
    CHECK_FALSE(script.talkable);
}

TEST_CASE("a Warrior's think sets the swap prompt", "[ai][swap]") {
    SwapScene s;
    s.scene.player().human().script().heldObject = 7.0;
    s.scene.run(5);
    CHECK(s.warrior->human().script().talkable);
}

TEST_CASE("the swap exchanges the held objects at once, one alone passing across", "[ai][swap]") {
    coney::human::ScriptState receiver;
    coney::human::ScriptState presser;
    receiver.heldObject = 5.0;
    receiver.heldObjectName = "bat";
    presser.heldObject = 6.0;
    presser.heldObjectName = "bottle";
    const auto any = [](std::string_view) { return true; };
    coney::ai::SwapOutcome outcome = coney::ai::swapHeldObjects(receiver, presser, any);
    CHECK(receiver.heldObject == 6.0);
    CHECK(receiver.heldObjectName == "bottle");
    CHECK(presser.heldObject == 5.0);
    CHECK(presser.heldObjectName == "bat");
    CHECK(outcome.toReceiver == 6.0);
    CHECK(outcome.toPresser == 5.0);
    // Only the presser's: it passes to the receiver.
    receiver.heldObject = 0.0;
    receiver.heldObjectName.clear();
    outcome = coney::ai::swapHeldObjects(receiver, presser, any);
    CHECK(receiver.heldObject == 5.0);
    CHECK(presser.heldObject == 0.0);
    CHECK(outcome.toPresser == 0.0);
}

TEST_CASE("a left-hand or hat object is dropped by the swap and taken by no hand", "[ai][swap]") {
    CHECK(coney::ai::swapPlaceable(1));
    CHECK_FALSE(coney::ai::swapPlaceable(5));
    CHECK_FALSE(coney::ai::swapPlaceable(6));
    coney::human::ScriptState receiver;
    coney::human::ScriptState presser;
    presser.heldObject = 6.0;
    presser.heldObjectName = "ring";
    const coney::ai::SwapOutcome outcome =
        coney::ai::swapHeldObjects(receiver, presser, [](std::string_view name) { return name != "ring"; });
    CHECK(receiver.heldObject == 0.0);
    CHECK(presser.heldObject == 0.0);
    CHECK(outcome.dropped == std::vector<double>{6.0});
}
