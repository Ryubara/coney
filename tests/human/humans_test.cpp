// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/humans.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

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
