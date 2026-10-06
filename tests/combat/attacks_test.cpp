// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/attacks.h"

#include <cstdint>

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

// Runs `chain` `updates` updates with no press and the record's +0x08 at `flags`; returns the last step.
ChainStep idle(AttackChain& chain, int updates, const CombatTuning& tuning, std::uint32_t flags) {
    ChainStep step;
    for (int i = 0; i < updates; ++i) {
        step = chain.update(ChainButton::None, flags, tuning);
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

TEST_CASE("the chain takes its phases from the record: a hit 2 updates in, the end once the clip's bits are gone",
          "[combat]") {
    const CombatTuning tuning;
    AttackChain chain;
    CHECK_FALSE(chain.start(anim_id::kAttackS1, tuning));
    CHECK(chain.comboCount() == 1);
    CHECK(chain.update(ChainButton::None, kPhaseWindUp, tuning).hit == anim_id::kNone);
    CHECK(chain.update(ChainButton::None, kPhaseWindUp, tuning).hit == anim_id::kAttackS1); // update 2
    // The window, the end phase and the recovery keep it going, whatever the age.
    for (const std::uint32_t phase : {kPhaseWindUp, kPhaseChainWindow, kPhaseEnd, kPhaseRecovery}) {
        CHECK_FALSE(idle(chain, 10, tuning, phase).finished);
        CHECK(chain.active());
    }
    // The clip has given back its bits: the attack is over and the combo starts again.
    const ChainStep end = chain.update(ChainButton::None, 0, tuning);
    CHECK(end.finished);
    CHECK_FALSE(chain.active());
    CHECK(chain.comboCount() == 0);
    // The counter's and the moving attacks' bits keep an attack going too.
    for (const std::uint32_t bit : {kPhaseCounter, kPhaseRunAttack}) {
        AttackChain other;
        other.start(anim_id::kAttackS1, tuning);
        CHECK_FALSE(idle(other, 5, tuning, bit).finished);
    }
}

TEST_CASE("a press in the wind-up is buffered and plays when the window opens; one in the end phase or recovery is "
          "dropped",
          "[combat]") {
    const CombatTuning tuning;
    AttackChain chain;
    chain.start(anim_id::kAttackS1, tuning);
    idle(chain, 4, tuning, kPhaseWindUp);
    CHECK(chain.update(ChainButton::Square, kPhaseWindUp, tuning).started == anim_id::kNone); // buffered
    CHECK(chain.buffered() == ChainButton::Square);
    const ChainStep opened = chain.update(ChainButton::None, kPhaseChainWindow, tuning); // the window opens
    CHECK(opened.started == anim_id::kAttackSS2);
    CHECK(chain.comboCount() == 2);

    // A later press replaces an earlier one: square, then cross, in SS2's wind-up gives SSX3.
    chain.update(ChainButton::Square, kPhaseWindUp, tuning);
    chain.update(ChainButton::Cross, kPhaseWindUp, tuning);
    CHECK(chain.update(ChainButton::None, kPhaseChainWindow, tuning).started == anim_id::kAttackSSX3);

    // In the end phase and the recovery a press is dropped and the attack ends with nothing after it.
    for (const std::uint32_t phase : {kPhaseEnd, kPhaseRecovery}) {
        AttackChain late;
        late.start(anim_id::kAttackS1, tuning);
        CHECK(late.update(ChainButton::Square, phase, tuning).started == anim_id::kNone);
        CHECK(late.buffered() == ChainButton::None);
        CHECK(late.update(ChainButton::None, 0, tuning).finished);
    }
}

TEST_CASE("presses in each wind-up give S1, SS2, SSS3 and a fourth press does nothing", "[combat]") {
    const CombatTuning tuning;
    AttackChain chain;
    chain.start(anim_id::kAttackS1, tuning);
    chain.update(ChainButton::Square, kPhaseWindUp, tuning);
    CHECK(chain.update(ChainButton::None, kPhaseChainWindow, tuning).started == anim_id::kAttackSS2);
    chain.update(ChainButton::Square, kPhaseWindUp, tuning);
    CHECK(chain.update(ChainButton::None, kPhaseChainWindow, tuning).started == anim_id::kAttackSSS3);
    CHECK(chain.comboCount() == 3);
    // SSS3's wind-up takes no press at a combo of 3, and its window has nothing to chain to.
    chain.update(ChainButton::Square, kPhaseWindUp, tuning);
    CHECK(chain.buffered() == ChainButton::None);
    CHECK(chain.update(ChainButton::Square, kPhaseChainWindow, tuning).started == anim_id::kNone);
    CHECK(chain.animId() == anim_id::kAttackSSS3);

    // X1, cross in its window: XX2; a third cross does nothing.
    AttackChain cross;
    cross.start(anim_id::kAttackX1, tuning);
    idle(cross, 9, tuning, kPhaseWindUp);
    CHECK(cross.update(ChainButton::Cross, kPhaseChainWindow, tuning).started == anim_id::kAttackXX2);
    CHECK(cross.update(ChainButton::Cross, kPhaseChainWindow, tuning).started == anim_id::kNone);
}

TEST_CASE("each attack hits at its measured update: X1 at 8, XX2 at 10, the power strike at once", "[combat]") {
    const CombatTuning tuning;
    CHECK(attackHitUpdate(anim_id::kAttackX1, tuning) == 8);
    CHECK(attackHitUpdate(anim_id::kAttackSSS3, tuning) == 7);
    CHECK(attackHitUpdate(anim_id::kGrabComboStrike3, tuning) == 1);
    CHECK(attackHitUpdate(anim_id::kSnapRight, tuning) == tuning.hitUpdate);

    // X1 then a cross in its window: XX2 hits 10 updates after it starts.
    AttackChain chain;
    CHECK_FALSE(chain.start(anim_id::kAttackX1, tuning));
    CHECK(idle(chain, 7, tuning, kPhaseWindUp).hit == anim_id::kNone);
    CHECK(chain.update(ChainButton::None, kPhaseWindUp, tuning).hit == anim_id::kAttackX1);           // update 8
    CHECK(chain.update(ChainButton::Cross, kPhaseWindUp, tuning).started == anim_id::kNone);          // buffered
    CHECK(chain.update(ChainButton::None, kPhaseChainWindow, tuning).started == anim_id::kAttackXX2); // update 10
    CHECK(idle(chain, 9, tuning, kPhaseWindUp).hit == anim_id::kNone);
    CHECK(chain.update(ChainButton::None, kPhaseWindUp, tuning).hit == anim_id::kAttackXX2);

    // The power strike's hit lands on its start.
    AttackChain power;
    CHECK(power.start(anim_id::kGrabPower1Strike1, tuning));
}

TEST_CASE("an anim set's square, cross and strikes, and the defaults for set 0", "[combat]") {
    const AnimSetClips none = animSetClips(0);
    CHECK(none.square == anim_id::kAttackS1);
    CHECK(none.cross == anim_id::kAttackX1);
    CHECK(none.grounded == anim_id::kGroundedStrike1);
    CHECK(none.mounting == anim_id::kMountingStrike);
    const AnimSetClips bat = animSetClips(3);
    CHECK(bat.square == 34);
    CHECK(bat.cross == 36);
    CHECK(bat.mounting == 38);
    CHECK(bat.grounded == 37);
    CHECK(animSetClips(1).square == 45);
    CHECK(animSetClips(2).cross == 41);
}
