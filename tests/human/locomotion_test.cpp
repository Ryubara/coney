// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/locomotion.h"

#include <array>
#include <bit>
#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Gait;
using coney::human::Speeds;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegree = kPi / 180.0F;

// Round speeds for the rules that read them: sneak 1.2, walk 1.5, jog 4, run 7.5, sprint 10.
Speeds testSpeeds() {
    return Speeds{.base = 3.0F, .sneak = 1.2F, .walk = 1.5F, .jog = 4.0F, .run = 7.5F, .sprint = 10.0F};
}

} // namespace

TEST_CASE("the stick does nothing inside the 0.12 dead zone, walks up to 0.95 and runs above", "[locomotion]") {
    const Speeds speeds = testSpeeds();
    CHECK(coney::human::targetSpeed(0.0F, speeds) == 0.0F);
    CHECK(coney::human::targetSpeed(0.10F, speeds) == 0.0F);
    CHECK(coney::human::targetSpeed(0.12F, speeds) == 0.0F);
    // Any deflection between the dead zone and the run threshold walks at the same speed.
    CHECK(coney::human::targetSpeed(0.13F, speeds) == 1.5F);
    CHECK(coney::human::targetSpeed(0.5F, speeds) == 1.5F);
    CHECK(coney::human::targetSpeed(0.94F, speeds) == 1.5F);
    CHECK(coney::human::targetSpeed(0.95F, speeds) == 1.5F);
    CHECK(coney::human::targetSpeed(0.96F, speeds) == 7.5F);
    CHECK(coney::human::targetSpeed(1.0F, speeds) == 7.5F);
}

TEST_CASE("the stick is turned into the camera's frame: up moves away from the camera", "[locomotion]") {
    // Camera looking along +y: stick up is +y (angle π/2), right is +x (angle 0).
    auto up = coney::human::stickIntent(0.0F, 0.6F, Vec3{0.0F, 1.0F, -0.3F});
    CHECK(up.angle == Approx(kPi / 2.0F));
    CHECK(up.magnitude == Approx(0.6F));
    auto right = coney::human::stickIntent(0.3F, 0.0F, Vec3{0.0F, 1.0F, 0.0F});
    CHECK(right.angle == Approx(0.0F).margin(1e-6));
    // Camera looking along +x: stick up is +x, so the heading (angle - π/2) faces +x, -90°.
    auto turned = coney::human::stickIntent(0.0F, 0.9F, Vec3{2.0F, 0.0F, 0.0F});
    CHECK(turned.angle == Approx(0.0F).margin(1e-6));
    const Vec3 direction = coney::human::facing(turned.angle - kPi / 2.0F);
    CHECK(direction.x == Approx(1.0F));
    CHECK(direction.y == Approx(0.0F).margin(1e-6));
    // The length is clamped to 1 (a diagonal of two full axes), and a locked stick is centred at π/2.
    CHECK(coney::human::stickIntent(1.0F, 1.0F, Vec3{0.0F, 1.0F, 0.0F}).magnitude == Approx(1.0F));
    auto locked = coney::human::stickIntent(1.0F, 0.0F, Vec3{0.0F, 1.0F, 0.0F}, true);
    CHECK(locked.magnitude == 0.0F);
    CHECK(locked.angle == Approx(kPi / 2.0F));
}

TEST_CASE("heading 0 faces +y and grows anticlockwise", "[locomotion]") {
    CHECK(coney::human::facing(0.0F).y == Approx(1.0F));
    CHECK(coney::human::facing(kPi / 2.0F).x == Approx(-1.0F));
    CHECK(coney::human::headingOf(Vec3{-1.0F, 0.0F, 0.0F}) == Approx(kPi / 2.0F));
    CHECK(coney::human::wrapAngle(3.0F * kPi) == Approx(kPi));
    CHECK(coney::human::wrapAngle(-kPi) == Approx(kPi));
}

TEST_CASE("a speed's gait: standing below 0.5 m/s, then the nearest gait speed", "[locomotion]") {
    const Speeds speeds = testSpeeds();
    CHECK(coney::human::gaitOfSpeed(0.49F, speeds) == Gait::Standing);
    CHECK(coney::human::gaitOfSpeed(0.8F, speeds) == Gait::Sneak);
    CHECK(coney::human::gaitOfSpeed(1.5F, speeds) == Gait::Walk);
    CHECK(coney::human::gaitOfSpeed(3.0F, speeds) == Gait::Jog);
    CHECK(coney::human::gaitOfSpeed(7.0F, speeds) == Gait::Run);
    CHECK(coney::human::gaitOfSpeed(9.5F, speeds) == Gait::Sprint);
    // Human_GaitForSpeed: at or above each threshold.
    CHECK(coney::human::gaitForSpeed(1.4F, speeds) == Gait::Standing);
    CHECK(coney::human::gaitForSpeed(1.5F, speeds) == Gait::Walk);
    CHECK(coney::human::gaitForSpeed(7.5F, speeds) == Gait::Run);
    CHECK(coney::human::gaitForSpeed(10.0F, speeds) == Gait::Sprint);
}

TEST_CASE("the player's turn limit in play is 20 degrees walking, 18 jogging and running, 16 sprinting, 24 in a stance",
          "[locomotion]") {
    CHECK(coney::human::maxTurn(Gait::Standing) == Approx(20.0F * kDegree));
    CHECK(coney::human::maxTurn(Gait::Walk) == Approx(20.0F * kDegree));
    CHECK(coney::human::maxTurn(Gait::Jog) == Approx(18.0F * kDegree));
    CHECK(coney::human::maxTurn(Gait::Run) == Approx(18.0F * kDegree));
    CHECK(coney::human::maxTurn(Gait::Sprint) == Approx(16.0F * kDegree));
    CHECK(coney::human::stanceTurn() == Approx(24.0F * kDegree));
}

TEST_CASE("CfgSetTurnRates and CfgTurnRate set the player's turn limits and ease, refusing angles of 90 or more",
          "[locomotion]") {
    const coney::human::LocomotionTuning saved = coney::human::locomotionTuning();
    // The script's order: two special states', sprint, run, jog, walk, stance; the first has no reader in Coney.
    const std::array<float, 6> rates{11.0F, 10.0F, 12.0F, 14.0F, 30.0F, 95.0F};
    coney::human::setPlayerTurnRates(rates);
    CHECK(coney::human::maxTurn(Gait::Sprint) == Approx(10.0F * kDegree));
    CHECK(coney::human::maxTurn(Gait::Run) == Approx(12.0F * kDegree));
    CHECK(coney::human::maxTurn(Gait::Jog) == Approx(14.0F * kDegree));
    CHECK(coney::human::maxTurn(Gait::Walk) == Approx(30.0F * kDegree));
    CHECK(coney::human::stanceTurn() == Approx(24.0F * kDegree)); // 95° is refused: the old value stays
    coney::human::setTurnEase(1.0F, 0.5F);
    CHECK(coney::human::locomotionTuning().turnEaseError == 1.0F);
    CHECK(coney::human::locomotionTuning().turnCarry == 0.5F);
    coney::human::locomotionTuning() = saved;
}

TEST_CASE("a 90 degree turn at a run goes 16 degrees, then 18 an update, as at runtime", "[locomotion]") {
    // The measured steps fit limit × (1 - cos(π e / 2.0)) / 2 + 0.8 × the last step, clamped to the limit.
    coney::human::TurnState state;
    const float limit = coney::human::maxTurn(Gait::Run);
    const float first = coney::human::turnToward(0.0F, 90.0F * kDegree, limit, state);
    CHECK(first / kDegree == Approx(16.0F).margin(0.1));
    const float second = coney::human::turnToward(first, 90.0F * kDegree, limit, state);
    CHECK((second - first) / kDegree == Approx(18.0F).margin(1e-3));
}

TEST_CASE("a turn is eased below 2.0 rad, carries 0.8 of the last step, and never passes the limit", "[locomotion]") {
    const float limit = 12.0F * kDegree;
    // A large error turns at the full limit.
    coney::human::TurnState state;
    float heading = coney::human::turnToward(0.0F, 2.5F, limit, state);
    CHECK(heading == Approx(limit));
    // A small error starts slowly: limit × (1 - cos(π e / 2.0)) / 2.
    coney::human::TurnState fresh;
    const float error = 0.3F;
    const float expected = limit * (1.0F - std::cos(kPi * error / 2.0F)) * 0.5F;
    CHECK(coney::human::turnToward(0.0F, error, limit, fresh) == Approx(expected));
    // The next step adds 0.8 of that one, clamped to the limit.
    const float second = coney::human::turnToward(expected, error, limit, fresh);
    const float remaining = error - expected;
    const float wanted = limit * (1.0F - std::cos(kPi * remaining / 2.0F)) * 0.5F + 0.8F * expected;
    CHECK(second == Approx(expected + std::min(wanted, limit)));
    // An error smaller than the step snaps onto the target.
    coney::human::TurnState carrying{.lastStep = limit, .lastError = 0.01F};
    CHECK(coney::human::turnToward(0.0F, 0.01F, limit, carrying) == Approx(0.01F));
    // Turning the shorter way round across ±π.
    coney::human::TurnState across;
    CHECK(coney::human::turnToward(3.0F, -3.0F, limit, across) > 3.0F - 1e-6F);
}

TEST_CASE("speed rises by 0.8 m/s an update and drops to its target at once", "[locomotion]") {
    const float step = coney::human::kStepSeconds;
    CHECK(coney::human::approachSpeed(0.0F, 7.5F, step) == Approx(0.8F));
    CHECK(coney::human::approachSpeed(7.0F, 7.5F, step) == Approx(7.5F));
    CHECK(coney::human::approachSpeed(7.5F, 0.0F, step) == 0.0F);
    CHECK(coney::human::approachSpeed(7.5F, 1.5F, step) == 1.5F);
}

TEST_CASE("a slope slows a grounded human, uphill and downhill alike", "[locomotion]") {
    CHECK(coney::human::slopeFactor(1.0F) == 1.0F);
    CHECK(coney::human::slopeFactor(0.95F) == 1.0F);
    CHECK(coney::human::slopeFactor(0.949F) == Approx(0.7347F).margin(1e-3));
    CHECK(coney::human::slopeFactor(0.87F) == Approx(0.711F));
    CHECK(coney::human::slopeFactor(0.1F) == Approx(0.5F));
}

TEST_CASE("the gait blend's target follows the speed piecewise between walk, jog, run and sprint", "[locomotion]") {
    const Speeds speeds = testSpeeds();
    CHECK(coney::human::gaitBlendForSpeed(0.0F, speeds) == 0.0F);
    CHECK(coney::human::gaitBlendForSpeed(1.5F, speeds) == 0.0F);
    CHECK(coney::human::gaitBlendForSpeed(2.75F, speeds) == Approx(0.5F));
    CHECK(coney::human::gaitBlendForSpeed(4.0F, speeds) == Approx(1.0F));
    CHECK(coney::human::gaitBlendForSpeed(7.5F, speeds) == Approx(2.0F));
    CHECK(coney::human::gaitBlendForSpeed(8.75F, speeds) == Approx(2.5F));
    CHECK(coney::human::gaitBlendForSpeed(50.0F, speeds) == Approx(3.0F));
}

TEST_CASE("a run skids to a stop when the stick lets go or turns back", "[locomotion]") {
    const Speeds speeds = testSpeeds();
    const Vec3 forward{0.0F, 1.0F, 0.0F};
    const Vec3 back{0.0F, -1.0F, 0.0F};
    CHECK(coney::human::skids(Gait::Run, 7.5F, speeds, 1.0F, 0.1F, forward, forward));
    CHECK(coney::human::skids(Gait::Run, 7.5F, speeds, 1.0F, 1.0F, forward, back));
    CHECK_FALSE(coney::human::skids(Gait::Run, 7.5F, speeds, 1.0F, 1.0F, forward, forward));
    // Not while walking, nor when the last stick was not a run.
    CHECK_FALSE(coney::human::skids(Gait::Walk, 1.5F, speeds, 1.0F, 0.1F, forward, back));
    CHECK_FALSE(coney::human::skids(Gait::Run, 7.5F, speeds, 0.9F, 0.1F, forward, back));
}

TEST_CASE("a run's release skids only when the measured speed is at least the run speed, exactly", "[locomotion]") {
    // The original compares the run speed with the measured speed as floats (c.le.S), with no slack: a speed one unit
    // in the last place short does not skid (docs/research/characters.md#run-stop).
    Speeds speeds = testSpeeds();
    speeds.run = std::bit_cast<float>(0x40f9a3adU); // 7.8012300, Rembrandt's
    const Vec3 forward{0.0F, 1.0F, 0.0F};
    const float below = std::nextafter(speeds.run, 0.0F);
    const float above = std::nextafter(speeds.run, 100.0F);
    CHECK_FALSE(coney::human::skids(Gait::Run, below, speeds, 1.0F, 0.0F, forward, forward));
    CHECK(coney::human::skids(Gait::Run, speeds.run, speeds, 1.0F, 0.0F, forward, forward));
    CHECK(coney::human::skids(Gait::Run, above, speeds, 1.0F, 0.0F, forward, forward));
    // A sprint's release is far above it.
    CHECK(coney::human::skids(Gait::Sprint, 10.245F, speeds, 1.0F, 0.0F, forward, forward));
}

TEST_CASE("the stick above 0.95 asks for the sprint speed only while sprinting", "[locomotion]") {
    const Speeds speeds = testSpeeds();
    CHECK(coney::human::targetSpeed(1.0F, speeds, true) == 10.0F);
    CHECK(coney::human::targetSpeed(0.96F, speeds, true) == 10.0F);
    CHECK(coney::human::targetSpeed(0.95F, speeds, true) == 1.5F);
    CHECK(coney::human::targetSpeed(0.5F, speeds, true) == 1.5F);
    CHECK(coney::human::targetSpeed(0.1F, speeds, true) == 0.0F);
}

TEST_CASE("the lean follows the turn by 0.625 a step, clamped by gait", "[locomotion]") {
    // A run turning left at 4° an update asks for 4° × 0.4 × 7.5 = 12°, clamped to 5°; the lean moves at most 1.3°.
    float lean = 0.0F;
    lean = coney::human::leanStep(lean, 4.0F * kDegree, 7.5F, Gait::Run);
    CHECK(lean == Approx(1.3F * kDegree));
    for (int i = 0; i < 30; ++i) {
        lean = coney::human::leanStep(lean, 4.0F * kDegree, 7.5F, Gait::Run);
    }
    CHECK(lean == Approx(5.0F * kDegree));
    // Turning right leans the other way; a walk is clamped to 2°, a sprint to 7°.
    float walk = 0.0F;
    float sprint = 0.0F;
    for (int i = 0; i < 30; ++i) {
        walk = coney::human::leanStep(walk, -12.0F * kDegree, 1.5F, Gait::Walk);
        sprint = coney::human::leanStep(sprint, 2.5F * kDegree, 10.0F, Gait::Sprint);
    }
    CHECK(walk == Approx(-2.0F * kDegree));
    CHECK(sprint == Approx(7.0F * kDegree));
    // No turn: the lean comes back by 0.625 of itself an update, at most the gait's rate.
    const float back = coney::human::leanStep(0.5F * kDegree, 0.0F, 7.5F, Gait::Run);
    CHECK(back == Approx(0.5F * kDegree * 0.375F));
}
