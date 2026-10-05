// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/attacks.h"

#include <catch2/catch_test_macros.hpp>

using namespace coney::combat;
using coney::human::Gait;

namespace {

// Square's attack for a stick, a gait and a target, with snaps on and no attack phase.
int square(Stick stick, Gait gait = Gait::Standing, TargetKind target = TargetKind::None, bool snaps = true,
           std::uint32_t phase = 0) {
    SquareInput input;
    input.target = target;
    input.stick = stick;
    input.gait = gait;
    input.phaseFlags = phase;
    input.snapAttacks = snaps;
    return squareAttack(input);
}

// Runs `chain` `updates` updates with no press; returns the last step.
ChainStep idle(AttackChain& chain, int updates, const CombatTuning& tuning) {
    ChainStep step;
    for (int i = 0; i < updates; ++i) {
        step = chain.update(ChainButton::None, tuning);
    }
    return step;
}

} // namespace

TEST_CASE("square picks its attack by target, then stick and gait", "[combat]") {
    // Standing, or a stick short of a snap: S1.
    CHECK(square({}) == anim_id::kAttackS1);
    CHECK(square({0.7F, 0.4F}) == anim_id::kAttackS1);
    // Full stick off the facing: a snap to that side, unless snaps are off.
    CHECK(square({0.98F, 0.1F}) == anim_id::kSnapRight);
    CHECK(square({-0.97F, -0.2F}) == anim_id::kSnapLeft);
    CHECK(square({0.1F, -0.99F}) == anim_id::kSnapBack);
    CHECK(square({0.98F, 0.1F}, Gait::Standing, TargetKind::None, false) == anim_id::kAttackS1);
    // Full stick ahead at a run: the run attack, but not while an attack phase is set.
    CHECK(square({0.05F, 0.99F}, Gait::Run) == anim_id::kAttackFromRun);
    CHECK(square({0.05F, 0.99F}, Gait::Run, TargetKind::None, true, kPhaseRecovery) == anim_id::kAttackS1);
    CHECK(square({0.0F, 0.9F}, Gait::Run) == anim_id::kAttackS1);
    // Walking with a partial stick: the walk attack; a stick inside the dead zone is not walking.
    CHECK(square({0.2F, 0.45F}, Gait::Walk) == anim_id::kAttackFromWalk);
    CHECK(square({0.0F, 0.12F}, Gait::Jog) == anim_id::kAttackFromWalk);
    CHECK(square({0.0F, 0.1F}, Gait::Walk) == anim_id::kAttackS1);
    // The target decides first.
    CHECK(square({0.98F, 0.1F}, Gait::Standing, TargetKind::Grounded) == anim_id::kGroundedStrike1);
    CHECK(square({}, Gait::Standing, TargetKind::Mounted) == anim_id::kMountingStrike);
    CHECK(square({}, Gait::Standing, TargetKind::Grabbed) == anim_id::kGrabFrontStrike);
    CHECK(square({}, Gait::Standing, TargetKind::Breakable) == anim_id::kNone);
    CHECK(crossAttack() == anim_id::kAttackX1);
}

TEST_CASE("an object attack picks its clip by the point's height above the feet", "[combat]") {
    CHECK(objectAttack(-0.2F) == anim_id::kGroundedStrike2);
    CHECK(objectAttack(0.5F) == anim_id::kBreakObjectLow);
    CHECK(objectAttack(0.8F) == anim_id::kBreakObjectLow);
    CHECK(objectAttack(1.1F) == anim_id::kBreakObjectMid);
}

TEST_CASE("the charge and the dive need a run with no attack phase, or a sprint", "[combat]") {
    CHECK(runningAttackAllowed(Gait::Run, 0));
    CHECK_FALSE(runningAttackAllowed(Gait::Run, kPhaseWindUp));
    CHECK(runningAttackAllowed(Gait::Sprint, kPhaseWindUp));
    CHECK_FALSE(runningAttackAllowed(Gait::Walk, 0));
    CHECK_FALSE(runningAttackAllowed(Gait::Standing, 0));
}

TEST_CASE("the chain table continues S1, X1 and SS2 and ends everything else", "[combat]") {
    CHECK(nextChainAttack(anim_id::kAttackS1, ChainButton::Square) == anim_id::kAttackSS2);
    CHECK(nextChainAttack(anim_id::kAttackS1, ChainButton::Cross) == anim_id::kAttackSX2);
    CHECK(nextChainAttack(anim_id::kAttackX1, ChainButton::Square) == anim_id::kAttackXS2);
    CHECK(nextChainAttack(anim_id::kAttackX1, ChainButton::Cross) == anim_id::kAttackXX2);
    CHECK(nextChainAttack(anim_id::kAttackSS2, ChainButton::Square) == anim_id::kAttackSSS3);
    CHECK(nextChainAttack(anim_id::kAttackSS2, ChainButton::Cross) == anim_id::kAttackSSX3);
    CHECK(nextChainAttack(anim_id::kAttackSSS3, ChainButton::Square) == anim_id::kNone);
    CHECK(nextChainAttack(anim_id::kAttackXX2, ChainButton::Cross) == anim_id::kNone);
    CHECK(nextChainAttack(anim_id::kAttackSX2, ChainButton::Square) == anim_id::kNone);
    // A buffered snap plays where a square would continue.
    CHECK(nextChainAttack(anim_id::kAttackS1, ChainButton::SnapLeft) == anim_id::kSnapLeft);
    CHECK(nextChainAttack(anim_id::kAttackSSS3, ChainButton::SnapBack) == anim_id::kNone);
}

TEST_CASE("the chain buffers cross's press and square's, the snap by the stick", "[combat]") {
    CHECK(chainButton(command::kCrossPressed, {}, true) == ChainButton::Cross);
    CHECK(chainButton(command::kCrossLongHold, {}, true) == ChainButton::None);
    CHECK(chainButton(command::kSquarePressed, {0.3F, 0.3F}, true) == ChainButton::Square);
    CHECK(chainButton(command::kSquarePressed, {0.99F, 0.0F}, true) == ChainButton::SnapRight);
    CHECK(chainButton(command::kSquarePressed, {0.99F, 0.0F}, false) == ChainButton::Square);
    CHECK(chainButton(command::kCircleTapped, {}, true) == ChainButton::None);
}

TEST_CASE("S1 hits 2 updates after the press, opens its chain window at 6 and ends at 20", "[combat]") {
    const CombatTuning tuning;
    AttackChain chain;
    chain.start(anim_id::kAttackS1);
    CHECK(chain.comboCount() == 1);
    CHECK(chain.phaseFlags(tuning) == kPhaseWindUp);
    CHECK(chain.update(ChainButton::None, tuning).hit == anim_id::kNone);
    CHECK(chain.update(ChainButton::None, tuning).hit == anim_id::kAttackS1); // update 2
    idle(chain, 3, tuning);
    CHECK(chain.phaseFlags(tuning) == kPhaseWindUp); // update 5
    idle(chain, 1, tuning);
    CHECK(chain.phaseFlags(tuning) == kPhaseChainWindow); // update 6
    idle(chain, 8, tuning);
    CHECK(chain.phaseFlags(tuning) == kPhaseChainWindow); // update 14
    idle(chain, 1, tuning);
    CHECK(chain.phaseFlags(tuning) == kPhaseEnd); // update 15
    idle(chain, 2, tuning);
    CHECK(chain.phaseFlags(tuning) == kPhaseRecovery); // update 17
    CHECK_FALSE(idle(chain, 2, tuning).finished);
    const ChainStep end = chain.update(ChainButton::None, tuning); // update 20
    CHECK(end.finished);
    CHECK_FALSE(chain.active());
    CHECK(chain.comboCount() == 0);
    CHECK(chain.phaseFlags(tuning) == 0);
}

TEST_CASE("a press in the wind-up is buffered and plays when the window opens; one in recovery is dropped",
          "[combat]") {
    const CombatTuning tuning;
    AttackChain chain;
    chain.start(anim_id::kAttackS1);
    idle(chain, 4, tuning);
    CHECK(chain.update(ChainButton::Square, tuning).started == anim_id::kNone); // update 5: buffered
    CHECK(chain.buffered() == ChainButton::Square);
    const ChainStep opened = chain.update(ChainButton::None, tuning); // update 6
    CHECK(opened.started == anim_id::kAttackSS2);
    CHECK(chain.comboCount() == 2);

    // A later press replaces an earlier one: square, then cross, in SS2's wind-up gives SSX3.
    chain.update(ChainButton::Square, tuning);
    chain.update(ChainButton::Cross, tuning);
    CHECK(idle(chain, 4, tuning).started == anim_id::kAttackSSX3);

    // In recovery a press is dropped and the attack ends with nothing after it.
    AttackChain late;
    late.start(anim_id::kAttackS1);
    idle(late, 18, tuning);
    CHECK(late.update(ChainButton::Square, tuning).started == anim_id::kNone); // update 19
    CHECK(late.update(ChainButton::None, tuning).finished);
}

TEST_CASE("a press every 6 updates gives S1, SS2, SSS3 and a fourth press does nothing", "[combat]") {
    const CombatTuning tuning;
    AttackChain chain;
    chain.start(anim_id::kAttackS1);
    idle(chain, 5, tuning);
    CHECK(chain.update(ChainButton::Square, tuning).started == anim_id::kAttackSS2);
    idle(chain, 5, tuning);
    CHECK(chain.update(ChainButton::Square, tuning).started == anim_id::kAttackSSS3);
    CHECK(chain.comboCount() == 3);
    // SSS3's wind-up takes no press at a combo of 3, and its window has nothing to chain to.
    chain.update(ChainButton::Square, tuning);
    CHECK(chain.buffered() == ChainButton::None);
    idle(chain, 6, tuning);
    CHECK(chain.update(ChainButton::Square, tuning).started == anim_id::kNone);
    CHECK(chain.animId() == anim_id::kAttackSSS3);

    // X1, cross: XX2; a third cross does nothing.
    AttackChain cross;
    cross.start(anim_id::kAttackX1);
    idle(cross, 7, tuning);
    CHECK(cross.update(ChainButton::Cross, tuning).started == anim_id::kAttackXX2);
    idle(cross, 7, tuning);
    CHECK(cross.update(ChainButton::Cross, tuning).started == anim_id::kNone);
}
