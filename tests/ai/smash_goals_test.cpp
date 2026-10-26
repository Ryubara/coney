// SPDX-License-Identifier: GPL-3.0-or-later
// The wreck's smash goals (docs/research/crew-wreck.md): DestroyItem's stand point and swing, and the smash target
// search. Synthetic humans on a flat floor; the level's side (ItemServices) is a fake.
#include "ai/smash_goals.h"

#include <cmath>
#include <memory>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/goal.h"
#include "support/ai_fixtures.h"
#include "support/fake_items.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::anim::Vec3;
using coney::test::AiScene;
using coney::test::FakeItems;

TEST_CASE("DestroyItem walks to the object's own spot and swings within 1 m of it", "[ai][steal][wreck]") {
    // DestroyItemGoal_Process (0x002dd210): Nav_FindPolygonUnder (0x00252410) never reads the stand-off or the
    // direction it is given, so the stand point is the object's spot on the ground; the swing comes within 1 m of it.
    AiScene scene;
    Brain& striker = scene.add({40.0F, 30.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.targets.push_back({.handle = 600, .position = {40.0F, 36.0F, 0.0F}, .animSet = 0});
    REQUIRE(striker.pushGoal(std::make_unique<coney::ai::DestroyItemGoal>(600.0, false, items)));
    const auto* goal = dynamic_cast<const coney::ai::DestroyItemGoal*>(striker.findGoal(GoalType::DestroyItem));
    REQUIRE(goal != nullptr);
    for (int k = 0; k < 60 * 10 && !goal->swung(); ++k) {
        scene.run(1);
    }
    REQUIRE(goal->swung());
    const Vec3 at = striker.human().position();
    CHECK(std::hypot(at.x - 40.0F, at.y - 36.0F) <= coney::ai::kDestroyItemReach + 0.05F);
}

TEST_CASE("the wreck's smash search refuses an object only with the player nearer than 1 m to it in 3D",
          "[ai][steal][wreck]") {
    // Ai_FindObject (0x0029d5f0): an object a player is nearer than 1 m to (Vec_DistSq < 1.0) is skipped. One on a
    // shelf 2 m above the player's feet is not; one at his feet is.
    AiScene scene;
    Brain& searcher = scene.add({42.0F, 40.0F, 0.0F}, 0.0F);
    const Vec3 feet = scene.player().human().position();
    FakeItems items;
    items.targets.push_back({.handle = 600, .position = {feet.x + 0.5F, feet.y, feet.z + 2.0F}, .animSet = 0});
    const std::optional<coney::ai::SmashTarget> above =
        coney::ai::findSmashTarget(searcher, scene.player(), items, scene.services, 15.0F, 15.0F);
    REQUIRE(above.has_value());
    CHECK(above->handle == 600.0);
    FakeItems low;
    low.targets.push_back({.handle = 601, .position = {feet.x + 0.5F, feet.y, feet.z}, .animSet = 0});
    CHECK_FALSE(coney::ai::findSmashTarget(searcher, scene.player(), low, scene.services, 15.0F, 15.0F).has_value());
}

TEST_CASE("the wreck's smash search refuses an object any player stands at, not only player 1", "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#stand-point, confirmed (code)
    // Ai_FindObject (0x0029d5f0) tests every player: a second player within 1 m of the object makes it refused.
    AiScene scene;
    Brain& searcher = scene.add({42.0F, 40.0F, 0.0F}, 0.0F);
    static_cast<void>(scene.add({44.5F, 40.0F, 0.0F}, 0.0F, coney::ai::BrainType::Player));
    FakeItems items;
    items.targets.push_back({.handle = 600, .position = {45.0F, 40.0F, 0.0F}, .animSet = 0});
    CHECK_FALSE(coney::ai::findSmashTarget(searcher, scene.player(), items, scene.services, 15.0F, 15.0F).has_value());
}
