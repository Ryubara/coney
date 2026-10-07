// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_reach.h"

#include <cstdint>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/attack_choice.h"

// The reach of each attack kind: the anim whose record gives the near and the far reach, or the fixed distance, by
// gait, the target's ground and the object in hand; and the start test's differences from the per-kind test.
// Research: docs/research/ai.md#attack-reach, docs/research/combat.md#ai-attacks

using coney::ai::ReachInput;

namespace {

// An input for `kind` with everything else at rest and empty-handed.
ReachInput of(int kind) {
    ReachInput input;
    input.kind = kind;
    return input;
}

// `kind` against a knocked-down target, at a run or a walk.
ReachInput moving(int kind, bool running, bool walking, bool targetDown) {
    ReachInput input = of(kind);
    input.running = running;
    input.walking = walking;
    input.targetDown = targetDown;
    return input;
}

// `kind` with an object of `set` in hand, the target beyond the near range or not, by a dealer or not.
ReachInput held(int kind, int set, bool beyond = false, bool dealer = false) {
    ReachInput input = of(kind);
    input.heldSet = set;
    input.beyondNearRange = beyond;
    input.dealer = dealer;
    return input;
}

// The near reach's anim of `input`, or none for a fixed distance.
std::optional<std::uint32_t> nearAnim(const ReachInput& input) { return coney::ai::nearReachSource(input).anim; }
// The far reach's anim.
std::optional<std::uint32_t> farAnim(const ReachInput& input) { return coney::ai::farReachSource(input).anim; }

} // namespace

TEST_CASE("an empty-handed kind's near reach is its anim's: X1 by gait and ground, S1 for the strikes", "[ai]") {
    CHECK(nearAnim(of(0)) == 11U);
    CHECK(nearAnim(moving(0, false, false, true)) == 194U);
    CHECK(nearAnim(moving(0, true, false, true)) == 24U);
    CHECK(nearAnim(moving(0, false, true, false)) == 23U);
    for (const int kind : {1, 3, 5, 6, 7, 8, 9}) {
        CHECK(nearAnim(of(kind)) == 12U);
    }
    CHECK(nearAnim(of(2)) == 11U);
    CHECK(nearAnim(of(4)) == 11U);
    CHECK(nearAnim(of(10)) == 25U);
    CHECK(nearAnim(of(22)) == 72U);
    CHECK(nearAnim(of(32)) == 104U);
    CHECK(coney::ai::nearReachSource(of(23)).metres == coney::ai::kThrowReach);
    CHECK_FALSE(nearAnim(of(24)).has_value());
    CHECK(coney::ai::nearReachSource(of(24)).metres == coney::ai::kNoClipReach);
}

TEST_CASE("the far reach takes each strike's own clip and the grab's 70", "[ai]") {
    CHECK(farAnim(moving(0, false, false, true)) == 11U);
    CHECK(farAnim(of(1)) == 12U);
    CHECK(farAnim(of(3)) == 15U);
    CHECK(farAnim(of(9)) == 20U);
    CHECK(farAnim(of(22)) == 70U);
    CHECK(farAnim(of(21)) == 3U);
}

TEST_CASE("a held object changes the strikes' reach by its set", "[ai]") {
    // A throwable thrown beyond the near range, swung within it, and never thrown by a dealer.
    CHECK(coney::ai::nearReachSource(held(0, 1, true)).metres == coney::ai::kThrowReach);
    CHECK(nearAnim(held(0, 1)) == 47U);
    CHECK(nearAnim(held(0, 1, true, true)) == 47U);
    CHECK(nearAnim(held(5, 1)) == 45U);
    CHECK(nearAnim(held(4, 2)) == 39U);
    CHECK(nearAnim(held(5, 3)) == 34U);
    CHECK(farAnim(held(5, 3)) == 35U);
    CHECK(coney::ai::nearReachSource(held(9, 4)).metres == coney::ai::kSet4Reach);
    CHECK(coney::ai::farReachSource(held(9, 5)).metres == coney::ai::kSet5Reach);
    // The specials keep their clips.
    CHECK(nearAnim(held(16, 2)) == 653U);
}

TEST_CASE("the start test differs from the per-kind test for the snap, the tackle's power and a rear-held grab",
          "[ai]") {
    const coney::ai::StartGuard guard;
    coney::ai::AttackerView a;
    coney::ai::TargetView t;
    // The snap needs no man beside him to start.
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 10));
    CHECK(coney::ai::canStartAttack(guard, a, t, 10));
    // The tackle needs a fifth of the power meter to start.
    CHECK(coney::ai::canUseAttackKind(a, t, 21));
    CHECK_FALSE(coney::ai::canStartAttack(guard, a, t, 21));
    a.grabPower = true;
    CHECK(coney::ai::canStartAttack(guard, a, t, 21));
    // A man held from the rear is grabbed only from in front.
    t.rearGrabbed = true;
    t.attackerInFront = false;
    CHECK(coney::ai::canUseAttackKind(a, t, 22));
    CHECK_FALSE(coney::ai::canStartAttack(guard, a, t, 22));
    t.attackerInFront = true;
    CHECK(coney::ai::canStartAttack(guard, a, t, 22));
    // Out of the far reach nothing starts.
    coney::ai::StartGuard far;
    far.inReach = false;
    CHECK_FALSE(coney::ai::canStartAttack(far, a, t, 22));
}
