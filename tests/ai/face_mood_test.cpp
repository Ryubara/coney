// SPDX-License-Identifier: GPL-3.0-or-later
// The mood a brain gives its human's face (ai/face_mood.h), picked from the brain's facts as the original's
// Brain_UpdateAlertness picks it: down, cuffed or burning first; then the gang's tactic; then the fight and chase
// goals and the top goal's type.
#include <catch2/catch_test_macros.hpp>

#include "ai/face_mood.h"
#include "human/face.h"

using coney::ai::FaceMoodFacts;
using coney::ai::faceMoodOf;
using coney::human::FaceMood;

TEST_CASE("a down, cuffed or burning human's face is scared whatever else holds", "[ai][face]") {
    // Expected: face.md#mood, confirmed (code)
    CHECK(faceMoodOf(FaceMoodFacts{.down = true, .fighting = true}) == FaceMood::Scared);
    CHECK(faceMoodOf(FaceMoodFacts{.cuffed = true, .gangTactic = true, .tacticAlerts = true}) == FaceMood::Scared);
    CHECK(faceMoodOf(FaceMoodFacts{.onFire = true, .topGoal = 0x4f}) == FaceMood::Scared);
}

TEST_CASE("without a gang tactic the face is neutral, angry with a fight or chase goal, and the top goal may decide",
          "[ai][face]") {
    // Expected: face.md#mood, confirmed (code)
    CHECK(faceMoodOf(FaceMoodFacts{}) == FaceMood::Neutral);
    CHECK(faceMoodOf(FaceMoodFacts{.fighting = true}) == FaceMood::Angry);
    CHECK(faceMoodOf(FaceMoodFacts{.chasing = true}) == FaceMood::Angry);
    // A top goal of no listed type keeps what the fight and chase tests gave.
    CHECK(faceMoodOf(FaceMoodFacts{.fighting = true, .topGoal = 0x05}) == FaceMood::Angry);
    CHECK(faceMoodOf(FaceMoodFacts{.topGoal = 0x05}) == FaceMood::Neutral);
    // Bums (0x4f, 0x50), rioters, dealers and the rest: excited, even in a fight.
    for (const int type : {0x4f, 0x50, 0x54, 0x62, 0x63, 0x71, 0x80}) {
        CHECK(faceMoodOf(FaceMoodFacts{.fighting = true, .topGoal = type}) == FaceMood::Excited);
    }
    for (const int type : {0x09, 0x4a, 0x52, 0x72, 0x7c, 0x8b, 0x98}) {
        CHECK(faceMoodOf(FaceMoodFacts{.topGoal = type}) == FaceMood::Angry);
    }
    // A fleeing pedestrian (0x6b), a lead chase, a gang call and the rest: scared, even with a fight goal.
    for (const int type : {0x02, 0x07, 0x11, 0x20, 0x49, 0x6b, 0x6d, 0x6e, 0x79}) {
        CHECK(faceMoodOf(FaceMoodFacts{.fighting = true, .topGoal = type}) == FaceMood::Scared);
    }
}

TEST_CASE("under a gang tactic the face is angry when it alerts the gang, else by the tactic's type", "[ai][face]") {
    // Expected: face.md#mood, confirmed (code)
    const FaceMoodFacts tactic{.gangTactic = true};
    CHECK(faceMoodOf(FaceMoodFacts{.gangTactic = true, .tacticAlerts = true, .tacticType = 0x18}) == FaceMood::Angry);
    // The goals no longer count under a tactic.
    CHECK(faceMoodOf(FaceMoodFacts{.gangTactic = true, .tacticType = 0x30, .fighting = true, .topGoal = 0x4f}) ==
          FaceMood::Neutral);
    // Types 0x13, 0x20 and 0x25: scared, unless a pad drives the human (then the neutral default).
    for (const int type : {0x13, 0x20, 0x25}) {
        FaceMoodFacts facts = tactic;
        facts.tacticType = type;
        CHECK(faceMoodOf(facts) == FaceMood::Scared);
        facts.padDriven = true;
        CHECK(faceMoodOf(facts) == FaceMood::Neutral);
    }
    for (const int type : {0x18, 0x1b, 0x24}) {
        FaceMoodFacts facts = tactic;
        facts.tacticType = type;
        CHECK(faceMoodOf(facts) == FaceMood::Excited);
    }
    for (const int type : {0x14, 0x15, 0x1c, 0x1d, 0x1e, 0x22, 0x23, 0x26}) {
        FaceMoodFacts facts = tactic;
        facts.tacticType = type;
        CHECK(faceMoodOf(facts) == FaceMood::Angry);
    }
}
