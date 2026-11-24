// SPDX-License-Identifier: GPL-3.0-or-later
// The input script's `drop-ai` (docs/guides/running-input.md#input-scripts): which AI human it picks and that the
// one it drops falls out of the world in the characters' step. A synthetic scene.
#include "debug/fighter_drop.h"

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "human/human.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::debug::dropBelowWorld;
using coney::debug::dropTarget;
using coney::debug::kDropDepth;
using coney::test::AiScene;

TEST_CASE("drop-ai picks the AI human by handle or the nearest to the player, never the player", "[debug][fighters]") {
    // Expected: unverified, #481 (a Coney debug aid; the fall it starts is characters-ground.md#out-of-world)
    AiScene scene; // the player stands at (40, 40)
    Brain& far = scene.add({60.0F, 40.0F, 0.0F}, 0.0F);
    Brain& near = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    CHECK(dropTarget(scene.brains, scene.player(), std::nullopt) == &near);
    CHECK(dropTarget(scene.brains, scene.player(), far.handle()) == &far);
    CHECK(dropTarget(scene.brains, scene.player(), scene.player().handle()) == nullptr);
    CHECK(dropTarget(scene.brains, scene.player(), 999.0) == nullptr);
    // A friend of the player's (his gang, as a level's crew) is never the nearest.
    coney::ai::Gangs& gangs = scene.brains.gangs();
    const int crew = gangs.create(0, "Warriors");
    gangs.addMember(crew, scene.player());
    gangs.addMember(crew, near);
    CHECK(dropTarget(scene.brains, scene.player(), std::nullopt) == &far);
    CHECK(dropTarget(scene.brains, scene.player(), near.handle()) == &near);
    gangs.removeMember(near);
    // One already out of the world is not picked again.
    dropBelowWorld(near);
    scene.run(1);
    CHECK(dropTarget(scene.brains, scene.player(), std::nullopt) == &far);
}

TEST_CASE("an AI human drop-ai drops is kDropDepth below where he stood and out of the world", "[debug][fighters]") {
    // Expected: unverified, #481 (a Coney debug aid; the fall it starts is characters-ground.md#out-of-world, confirmed
    // (code) at 0x0023fea8)
    AiScene scene;
    Brain& fighter = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    const float before = fighter.human().position().z;
    dropBelowWorld(fighter);
    CHECK(fighter.human().position().z == before - kDropDepth);
    CHECK(fighter.human().position().x == 44.0F);
    scene.run(1);
    CHECK(fighter.human().outOfWorld());
    CHECK_FALSE(scene.player().human().outOfWorld());
}
