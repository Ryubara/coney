// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/ai_counter.h"

#include <numbers>

#include <catch2/catch_test_macros.hpp>

// The AI's counter (command 3): which intro it answers and when the human pressing it may counter.
// Research: docs/research/ai.md#block

using coney::combat::AiCounterSide;

TEST_CASE("the AI's counter answers a grab's intro with 76 and a tackle's with 9", "[combat]") {
    for (const int grab : {69, 70, 71}) {
        CHECK(coney::combat::aiCounterFor(grab) == coney::combat::kAiGrabCounter);
    }
    for (const int tackle : {2, 3, 4}) {
        CHECK(coney::combat::aiCounterFor(tackle) == coney::combat::kAiTackleCounter);
    }
    // A strike, the grab's hold or the tackle's hit: nothing to counter.
    for (const int other : {0, 1, 5, 11, 12, 68, 72, 82}) {
        CHECK(coney::combat::aiCounterFor(other) == coney::combat::anim_id::kNone);
    }
}

TEST_CASE("only a free, unhurt, empty-handed human no pad drives may counter", "[combat]") {
    // A side changed by `change` from a free human's.
    const auto side = [](auto change) {
        AiCounterSide made;
        change(made);
        return coney::combat::aiCounterAllowed(made);
    };
    CHECK(side([](AiCounterSide& /*s*/) {}));
    CHECK_FALSE(side([](AiCounterSide& s) { s.padControlled = true; }));
    CHECK_FALSE(side([](AiCounterSide& s) { s.standing = false; }));
    CHECK_FALSE(side([](AiCounterSide& s) { s.holdingObject = true; }));
    CHECK_FALSE(side([](AiCounterSide& s) { s.hurt = true; }));
    // An attack's phase (0x1), the grab bit (0x10) and the duck (0x01000000) refuse; a bit outside both masks does not.
    CHECK_FALSE(side([](AiCounterSide& s) { s.phaseFlags = 0x1; }));
    CHECK_FALSE(side([](AiCounterSide& s) { s.phaseFlags = 0x10; }));
    CHECK_FALSE(side([](AiCounterSide& s) { s.phaseFlags = 0x01000000; }));
    CHECK(side([](AiCounterSide& s) { s.phaseFlags = 0x100; }));
}

TEST_CASE("face to face needs each human in the other's front quarter", "[combat]") {
    constexpr float kPi = std::numbers::pi_v<float>;
    const coney::anim::Vec3 a{0.0F, 0.0F, 0.0F};
    const coney::anim::Vec3 ahead{0.0F, 1.0F, 0.0F};
    // a faces +y (heading 0); the other stands ahead of it.
    CHECK(coney::combat::faceToFace(a, 0.0F, ahead, kPi));
    // Ahead but turned away: a stands behind it.
    CHECK_FALSE(coney::combat::faceToFace(a, 0.0F, ahead, 0.0F));
    // Facing a, but a turned away from it.
    CHECK_FALSE(coney::combat::faceToFace(a, kPi, ahead, kPi));
}
