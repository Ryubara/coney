// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/humans.h"

#include <cstddef>
#include <cstdint>
#include <memory>
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
// AI's attack action writes it (docs/research/ai-fights.md#attack-action). Synthetic clips only.

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
    pair.other.spawn(pair.mesh.get(), coney::anim::Vec3{40.0F, 41.0F, 0.0F}, 180.0F);
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

TEST_CASE("a held human neither animates, moves nor acts while the others do", "[human][humans][debug]") {
    // The Entity inspector's freeze for a human other than the player (docs/guides/debug-menu.md#entity-inspector):
    // Coney's own debug tooling, so the original's order is untouched for a human that is not held.
    Pair pair;
    CHECK_FALSE(pair.humans.held(pair.other));
    pair.humans.setHeld(pair.other, true);
    CHECK(pair.humans.held(pair.other));
    CHECK_FALSE(pair.humans.held(pair.player));
    // The brain presses square every step and pushes the stick: a held human does neither, a pad's human is untouched.
    pair.humans.setBrains([](std::span<Human* const> humans) {
        REQUIRE(humans.size() == 2);
        humans[1]->record().command = command::kSquarePressed;
        humans[1]->record().stickY = 0.6F;
        humans[1]->record().cameraForward = coney::test::kAlongY;
    });
    const coney::anim::Vec3 before = pair.other.position();
    for (int k = 0; k < 30; ++k) {
        pair.humans.update(pair.mesh.get());
    }
    CHECK(pair.other.position().x == before.x);
    CHECK(pair.other.position().y == before.y);
    CHECK(pair.other.animator().animId() == 388U);
    CHECK(pair.humans.steps() == 30U);
    // Let go, and the same brain's commands act at once.
    pair.humans.setHeld(pair.other, false);
    CHECK_FALSE(pair.humans.held(pair.other));
    for (int k = 0; k < 30; ++k) {
        pair.humans.update(pair.mesh.get());
    }
    CHECK(pair.other.animator().animId() != 388U);
}

TEST_CASE("a held human stays held when an earlier slot is removed, and a removed one is no longer held",
          "[human][humans][debug]") {
    // Expected: ../guides/debug-menu.md#entity-inspector, Coney's own debug tooling (no original value)
    Pair pair;
    pair.humans.setHeld(pair.other, true);
    pair.humans.remove(pair.player);
    CHECK(pair.humans.held(pair.other));
    CHECK_FALSE(pair.humans.held(pair.player));
    pair.humans.remove(pair.other);
    CHECK_FALSE(pair.humans.held(pair.other));
    // Holding a human that is not in the step does nothing.
    pair.humans.setHeld(pair.other, true);
    CHECK_FALSE(pair.humans.held(pair.other));
}

namespace {

// A crowd of the synthetic fighting character on a 400 m floor: slot 0 the pad's player, the rest driven only by the
// brains' hook, each placed where `at` says (issue 1550).
struct Crowd {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 400.0F, 0.0F, 400.0F));
    std::vector<std::unique_ptr<Human>> people;
    Humans humans;

    explicit Crowd(const std::vector<coney::anim::Vec3>& at) {
        for (std::size_t slot = 0; slot < at.size(); ++slot) {
            people.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                     coney::test::identityBind(), 1.0F, &character.ranges));
            people.back()->spawn(mesh.get(), at[slot], 0.0F);
            humans.add(*people.back(), slot == 0);
        }
    }
};

// Which of two enemies the player's square takes.
enum class Picked : std::uint8_t { None, Left, Right };

// The square's target the player at (40, 40), facing +y, picks from two enemies 1 m ahead and half a metre to either
// side, at the same distance and angle: the left one in slot `leftSlot` and the right one in the other of slots 1 and
// 2, with a third enemy far off at (`farX`, `farY`). With `odd` the pick is on an odd step (actions last to first).
Picked tiedPick(std::size_t leftSlot, float farX, float farY, bool odd) {
    const coney::anim::Vec3 left{39.5F, 41.0F, 0.0F};
    const coney::anim::Vec3 right{40.5F, 41.0F, 0.0F};
    Crowd crowd({coney::anim::Vec3{40.0F, 40.0F, 0.0F}, leftSlot == 1 ? left : right, leftSlot == 1 ? right : left,
                 coney::anim::Vec3{farX, farY, 0.0F}});
    if (odd) {
        crowd.humans.update(crowd.mesh.get());
    }
    crowd.people[0]->record().command = command::kSquarePressed;
    crowd.humans.update(crowd.mesh.get());
    const coney::human::Combatant* target = crowd.people[0]->fighter().target();
    const std::size_t rightSlot = leftSlot == 1 ? 2 : 1;
    if (target == crowd.people[leftSlot].get()) {
        return Picked::Left;
    }
    return target == crowd.people[rightSlot].get() ? Picked::Right : Picked::None;
}

} // namespace

TEST_CASE("the actions pass asks whether two humans are opposed only about the humans near the one acting",
          "[human][humans]") {
    // Forty humans 20 m apart, every one opposed to every other: none is within any search's reach of another, so the
    // searches need not ask about any pair. A list of every opposed human for every human every step asked about
    // each of the 40 x 39 pairs (issue 1550).
    // Expected: combat-targets.md#searches-coney, confirmed (code)
    std::vector<coney::anim::Vec3> at;
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 8; ++column) {
            at.push_back(coney::anim::Vec3{20.0F + (20.0F * static_cast<float>(column)),
                                           20.0F + (20.0F * static_cast<float>(row)), 0.0F});
        }
    }
    Crowd crowd(at);
    std::size_t asked = 0;
    crowd.humans.setOpposition([&asked](const Human& attacker, const Human& victim) {
        ++asked;
        return &attacker != &victim;
    });
    constexpr int kSteps = 3;
    for (int k = 0; k < kSteps; ++k) {
        crowd.people[0]->record().command = command::kSquarePressed;
        crowd.humans.update(crowd.mesh.get());
    }
    CHECK(asked < static_cast<std::size_t>(kSteps) * 40U * 39U / 20U);
}

// A stand-in pin: Coney's searches give a tie to the later slot, where the original's sort (SortByKeyAscending
// 0x003868d0) keeps the earlier one first; #1590 makes Coney do the same and turns these expectations round. Until
// then the test holds the grid to the rule the searches had before it (a refactor pin, not the original's rule).
TEST_CASE("of two enemies the same distance and angle away, square takes the one in the later slot",
          "[human][humans][targeting]") {
    // The searches visit the targets in slot order and the nearest-wins test keeps the last of equals
    // (nearestFightable(), `distance <= best`), so a tie falls to the later slot whichever side it stands on and
    // whichever way the actions pass runs. The third enemy, 30 m off at x 37.5, puts the left and the right one in
    // different cells of the targets' grid (2.5 m cells from the lowest x and y), the later slot's first or last; at
    // (70, 39) both are in one cell.
    // Expected: combat-targets.md#searches-coney, Coney's stand-in (#1590): the original keeps the earlier slot
    for (const bool odd : {false, true}) {
        CHECK(tiedPick(1, 37.5F, 70.0F, odd) == Picked::Right);
        CHECK(tiedPick(2, 37.5F, 70.0F, odd) == Picked::Left);
        CHECK(tiedPick(1, 70.0F, 39.0F, odd) == Picked::Right);
        CHECK(tiedPick(2, 70.0F, 39.0F, odd) == Picked::Left);
    }
}

TEST_CASE("a human more than 20 m below the mesh's lowest point fades by 10 an update and is destroyed once faded",
          "[human][humans]") {
    // Expected: characters.md#out-of-world, confirmed (code) at 0x0023fea8 (the bit) and 0x00249108 (the fade and the
    // destroy), confirmed (runtime) run r-20261122-fall-out-of-world (255 before, 5 at Human_Destroy)
    Pair pair;
    std::vector<int> destroyedAt;
    int step = 0;
    pair.humans.setDestroy([&](Human& human) {
        CHECK(&human == &pair.other);
        destroyedAt.push_back(step);
    });
    CHECK(pair.other.fade() == 255);
    // More than 20 m below the mesh's lowest point (the synthetic header's -50): the state pass flags him, the actions
    // pass fades him.
    pair.other.place(pair.mesh.get(), coney::anim::Vec3{45.0F, 40.0F, -100.0F}, 0.0F);
    for (step = 1; step <= 25; ++step) {
        pair.humans.update(pair.mesh.get());
        REQUIRE(pair.other.outOfWorld());
        CHECK(pair.other.fade() == 255 - (10 * step));
    }
    CHECK(destroyedAt.empty());
    // Faded below 11: destroyed on the next update, his fade left as it was.
    step = 26;
    pair.humans.update(pair.mesh.get());
    CHECK(destroyedAt == std::vector<int>{26});
    CHECK(pair.other.fade() == 5);
    // The player in the same step was neither faded nor destroyed.
    CHECK_FALSE(pair.player.outOfWorld());
    CHECK(pair.player.fade() == 255);
    // Put back (Coney's choice for a player), he stands whole again.
    pair.other.putBack(pair.mesh.get(), coney::anim::Vec3{45.0F, 40.0F, 0.0F}, 0.0F);
    CHECK_FALSE(pair.other.outOfWorld());
    CHECK(pair.other.fade() == 255);
}
