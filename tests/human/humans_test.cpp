// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/humans.h"

#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/locomotion_gate.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The characters' step (docs/research/tasks.md#humans-update): every human driven through its per-player record, the
// passes in the original's order, and a human no pad drives fighting from a command written at the brains' place, as an
// AI's attack action writes it (docs/research/ai.md#attack-action). Synthetic clips only.

using coney::human::Human;
using coney::human::Humans;
namespace id = coney::combat::anim_id;
namespace command = coney::combat::command;

namespace {

// Two humans of the synthetic fighting character on a floor, 5 m apart: slot 0 the pad's, slot 1 driven only by what
// the brains' hook writes.
struct Pair {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human player{character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges};
    Human other{character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                &character.ranges};
    Humans humans;

    Pair() {
        player.spawn(mesh.get(), coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        other.spawn(mesh.get(), coney::anim::Vec3{45.0F, 40.0F, 0.0F}, 0.0F);
        humans.add(player, true);
        humans.add(other, false);
    }
};

} // namespace

TEST_CASE("a human no pad drives attacks from the command its brain writes, through the player's dispatcher",
          "[human][humans]") {
    Pair pair;
    // The brain presses square on step 10 and 11 (as the attack action writes its command again while it waits).
    std::uint64_t step = 0;
    pair.humans.setBrains([&](std::span<Human* const> humans) {
        REQUIRE(humans.size() == 2);
        if (step == 10 || step == 11) {
            humans[1]->record().command = command::kSquarePressed;
        }
        ++step;
    });
    std::vector<std::uint32_t> clips;
    std::vector<std::uint32_t> flags;
    for (int k = 0; k < 40; ++k) {
        pair.humans.update(pair.mesh.get());
        clips.push_back(pair.other.animator().animId());
        flags.push_back(pair.other.animator().flags());
    }
    REQUIRE(clips.size() == 40);
    // S1 starts on the step the command is written and holds its wind-up; the second press, in the wind-up, is
    // buffered and plays SS2 when S1's window opens (its clip's event at frame 5, 7 updates in at rate 0.75).
    CHECK(clips[9] != static_cast<std::uint32_t>(id::kAttackS1));
    CHECK(clips[10] == static_cast<std::uint32_t>(id::kAttackS1));
    CHECK(flags[10] == coney::combat::kPhaseWindUp);
    CHECK(clips[16] == static_cast<std::uint32_t>(id::kAttackS1));
    CHECK(flags[16] == coney::combat::kPhaseWindUp);
    CHECK(clips[17] == static_cast<std::uint32_t>(id::kAttackSS2));
    // The player, whose pad wrote nothing, stood still.
    CHECK(pair.player.animator().animId() == 388U);
    CHECK(pair.humans.steps() == 40U);
}

TEST_CASE("a record no pad drives loses its command at the step's start; a pad's keeps it", "[human][humans]") {
    Pair pair;
    // A command left in the AI's record from before the step is cleared: only the brain's write counts.
    pair.other.record().command = command::kSquarePressed;
    pair.player.record().command = command::kSquarePressed;
    pair.humans.update(pair.mesh.get());
    CHECK(pair.other.record().command == command::kNone);
    CHECK(pair.other.animator().animId() == 388U);
    CHECK(pair.player.animator().animId() == static_cast<std::uint32_t>(id::kAttackS1));
}

TEST_CASE("the step animates every human, then moves every one, then runs their actions", "[human][humans]") {
    Pair pair;
    // An attack started in the actions pass has not advanced in that step: its clip is at its start, and its first
    // advance is the next step's animation pass.
    pair.humans.setBrains([](std::span<Human* const> humans) {
        REQUIRE(humans.size() == 2);
        humans[1]->record().command = command::kSquarePressed;
    });
    pair.humans.update(pair.mesh.get());
    REQUIRE(pair.other.animator().tasks().top() != nullptr);
    CHECK(pair.other.animator().animId() == static_cast<std::uint32_t>(id::kAttackS1));
    CHECK(pair.other.animator().tasks().top()->time() == 0.0F);
    pair.humans.setBrains({});
    pair.humans.update(pair.mesh.get());
    REQUIRE(pair.other.animator().tasks().top() != nullptr);
    CHECK(pair.other.animator().tasks().top()->time() > 0.0F);
    // The stick in the record moves a human no pad drives, as a brain's move action will.
    Pair walker;
    for (int k = 0; k < 30; ++k) {
        walker.other.record().stickY = 0.6F;
        walker.other.record().cameraForward = coney::test::kAlongY;
        walker.humans.update(walker.mesh.get());
    }
    CHECK(walker.other.position().y > 40.5F);
    CHECK(walker.player.position().y == 40.0F);
}

TEST_CASE("the step every human advances by is the one given: slow motion's 1/150 s plays a fifth as far",
          "[human][humans]") {
    // The same attack started on both pairs, then one step at 1/30 s and one at 1/150 s
    // (docs/research/camera.md#slow-motion).
    Pair normal;
    Pair slow;
    for (Pair* pair : {&normal, &slow}) {
        pair->player.record().command = command::kSquarePressed;
        pair->humans.update(pair->mesh.get());
        pair->player.record().command = command::kNone;
    }
    normal.humans.update(normal.mesh.get(), {}, coney::human::kStepSeconds);
    slow.humans.update(slow.mesh.get(), {}, coney::human::kStepSeconds / 5.0F);
    CHECK(slow.player.stepSeconds() == coney::human::kStepSeconds / 5.0F);
    CHECK(slow.other.stepSeconds() == coney::human::kStepSeconds / 5.0F);
    REQUIRE(normal.player.animator().tasks().top() != nullptr);
    REQUIRE(slow.player.animator().tasks().top() != nullptr);
    CHECK(slow.player.animator().tasks().top()->time() * 5.0F ==
          Catch::Approx(normal.player.animator().tasks().top()->time()));
}

TEST_CASE("a reaction to a player's hit asks for a shake at the hit code's strength", "[human][humans]") {
    // The other human 1 m in front of the player, facing him; the player's square lands on it
    // (docs/research/camera.md#shake).
    Pair pair;
    pair.other.spawn(pair.mesh.get(), coney::anim::Vec3{40.0F, 41.0F, 0.0F}, std::numbers::pi_v<float>);
    std::optional<coney::human::ReactionShake> seen;
    for (int k = 0; k < 30 && !seen; ++k) {
        pair.player.record().command = k == 0 ? command::kSquarePressed : command::kNone;
        pair.humans.update(pair.mesh.get());
        seen = pair.other.fighter().reactionShake();
        CHECK_FALSE(pair.player.fighter().reactionShake().has_value());
    }
    REQUIRE(seen.has_value());
    CHECK(seen.value_or(coney::human::ReactionShake{}).attackerIsPlayer);
    // S1's hit code 0x09: strength 0, which stops a shake.
    CHECK(seen.value_or(coney::human::ReactionShake{.level = -1}).level == 0);
    // The next update asks for nothing more.
    pair.humans.update(pair.mesh.get());
    CHECK_FALSE(pair.other.fighter().reactionShake().has_value());
}
