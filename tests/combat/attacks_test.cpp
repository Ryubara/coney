// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/attacks.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using namespace coney::combat;
using coney::human::Gait;

namespace {

// Square's attack for a stick, a gait and a target, with snaps needing a target, a human found for a snap and no attack
// phase.
int square(Stick stick, Gait gait = Gait::Standing, TargetKind target = TargetKind::None, bool needsTarget = true,
           std::uint32_t phase = 0, bool snapTarget = true) {
    SquareInput input;
    input.target = target;
    input.stick = stick;
    input.gait = gait;
    input.phaseFlags = phase;
    input.snapNeedsTarget = needsTarget;
    input.snapTarget = snapTarget;
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
    // Full stick off the facing with a human found there: a snap to that side.
    CHECK(square({0.98F, 0.1F}) == anim_id::kSnapRight);
    CHECK(square({-0.97F, -0.2F}) == anim_id::kSnapLeft);
    CHECK(square({0.1F, -0.99F}) == anim_id::kSnapBack);
    // No human there (or only the current target): no snap, square goes on to S1; with `CfgSnap` off the stick alone
    // snaps.
    CHECK(square({0.98F, 0.1F}, Gait::Standing, TargetKind::None, true, 0, false) == anim_id::kAttackS1);
    CHECK(square({0.98F, 0.1F}, Gait::Standing, TargetKind::None, false, 0, false) == anim_id::kSnapRight);
    CHECK(square({0.68F, 0.72F}, Gait::Standing, TargetKind::None, false, 0, false) == anim_id::kAttackS1);
    // Full stick ahead, or under 45° off it, is no snap.
    CHECK(square({0.0F, 0.99F}) == anim_id::kAttackS1);
    CHECK(square({0.68F, 0.72F}) == anim_id::kAttackS1);
    // The run and walk attacks come first: a snap comes from a player standing (or sprinting).
    CHECK(square({0.98F, 0.1F}, Gait::Run) == anim_id::kAttackFromRun);
    CHECK(square({0.98F, 0.1F}, Gait::Walk) == anim_id::kAttackFromWalk);
    CHECK(square({0.1F, -0.99F}, Gait::Jog) == anim_id::kAttackFromWalk);
    CHECK(square({0.98F, 0.1F}, Gait::Sprint) == anim_id::kSnapRight);
    CHECK(square({0.98F, 0.1F}, Gait::Run, TargetKind::None, true, kPhaseRecovery) == anim_id::kSnapRight);
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
    CHECK(crossAttack(SquareInput{}) == anim_id::kAttackX1);
}

TEST_CASE("the stick asks for a snap beyond 0.95 and more than 45 degrees off the facing", "[combat]") {
    CHECK(snapForStick({0.96F, 0.0F}) == anim_id::kSnapRight);
    CHECK(snapForStick({-0.96F, 0.0F}) == anim_id::kSnapLeft);
    CHECK(snapForStick({0.0F, -0.96F}) == anim_id::kSnapBack);
    CHECK(snapForStick({0.94F, 0.0F}) == anim_id::kNone);
    CHECK(snapForStick({0.0F, 1.0F}) == anim_id::kNone);
    CHECK(snapForStick({0.68F, 0.72F}) == anim_id::kNone); // under 45°: the front
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
    CHECK(chainButton(command::kCrossPressed, {}) == ChainButton::Cross);
    CHECK(chainButton(command::kCrossLongHold, {}) == ChainButton::None);
    CHECK(chainButton(command::kSquarePressed, {0.3F, 0.3F}) == ChainButton::Square);
    CHECK(chainButton(command::kSquarePressed, {0.99F, 0.0F}) == ChainButton::SnapRight);
    CHECK(chainButton(command::kCircleTapped, {}) == ChainButton::None);
    CHECK(chainSnap(ChainButton::SnapBack) == anim_id::kSnapBack);
    CHECK(chainSnap(ChainButton::Square) == anim_id::kNone);
}

TEST_CASE("a buffered snap plays only with a snap target in reach, else the square step; CfgSnap off needs none",
          "[combat]") {
    // S1's window with a left snap buffered.
    const auto play = [](bool snapInReach, bool needsTarget) {
        CombatTuning tuning;
        tuning.snapNeedsTarget = needsTarget;
        AttackChain chain;
        chain.start(anim_id::kAttackS1, tuning);
        chain.update(ChainButton::SnapLeft, kPhaseWindUp, tuning);
        return chain.update(ChainButton::None, kPhaseChainWindow, tuning, ChainTargets{.snapInReach = snapInReach})
            .started;
    };
    CHECK(play(true, true) == anim_id::kSnapLeft);
    CHECK(play(false, true) == anim_id::kAttackSS2);
    CHECK(play(false, false) == anim_id::kSnapLeft);
}

TEST_CASE("no step from S1 or X1 plays at a low target; SS2's step still does", "[combat]") {
    const CombatTuning tuning;
    const ChainTargets low{.targetLow = true};
    for (const int first : {anim_id::kAttackS1, anim_id::kAttackX1}) {
        AttackChain chain;
        chain.start(first, tuning);
        chain.update(ChainButton::Square, kPhaseWindUp, tuning, low);
        CHECK(chain.update(ChainButton::None, kPhaseChainWindow, tuning, low).started == anim_id::kNone);
        // The press is dropped: the target getting up later in the window plays nothing.
        CHECK(chain.buffered() == ChainButton::None);
        CHECK(chain.update(ChainButton::None, kPhaseChainWindow, tuning).started == anim_id::kNone);
        CHECK(chain.animId() == first);
    }
    AttackChain second;
    second.start(anim_id::kAttackS1, tuning);
    second.update(ChainButton::Square, kPhaseChainWindow, tuning);
    second.update(ChainButton::Square, kPhaseWindUp, tuning, low);
    CHECK(second.update(ChainButton::None, kPhaseChainWindow, tuning, low).started == anim_id::kAttackSSS3);
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
    // The knife's and the baton's (combat.md#bat): square, cross, mounting (0x12) and grounded (0x13).
    const AnimSetClips knife = animSetClips(1);
    CHECK(knife.square == 45);
    CHECK(knife.cross == 47);
    CHECK(knife.mounting == 49);
    CHECK(knife.grounded == 48);
    const AnimSetClips baton = animSetClips(2);
    CHECK(baton.square == 39);
    CHECK(baton.cross == 41);
    CHECK(baton.mounting == 43);
    CHECK(baton.grounded == 42);
    // The throwing sets install no swing.
    CHECK(animSetClips(4).square == anim_id::kAttackS1);
}

TEST_CASE("cross: the run attack at a run or a sprint, the walk attack at a walk, else X1; no snaps", "[combat]") {
    const auto cross = [](Stick stick, Gait gait, std::uint32_t phase = 0) {
        return crossAttack(SquareInput{.stick = stick, .gait = gait, .phaseFlags = phase, .snapTarget = true});
    };
    CHECK(cross({0.0F, 1.0F}, Gait::Run) == anim_id::kAttackFromRun);
    CHECK(cross({0.0F, 1.0F}, Gait::Sprint) == anim_id::kAttackFromRun);
    CHECK(cross({0.0F, 1.0F}, Gait::Run, 0x10000000) == anim_id::kAttackX1);
    CHECK(cross({0.0F, 0.9F}, Gait::Run) == anim_id::kAttackX1);
    CHECK(cross({0.0F, 0.6F}, Gait::Walk) == anim_id::kAttackFromWalk);
    CHECK(cross({0.0F, 0.1F}, Gait::Walk) == anim_id::kAttackX1);
    CHECK(cross({}, Gait::Standing) == anim_id::kAttackX1);
    // Full stick to a side with a human there: still X1.
    CHECK(cross({1.0F, 0.0F}, Gait::Standing) == anim_id::kAttackX1);
}

TEST_CASE("a knife, baton or bat in hand: 501 at a run, the slot's swing otherwise, no walk attack or snaps",
          "[combat]") {
    for (const int set : {1, 2, 3}) {
        INFO("set " << set);
        const AnimSetClips clips = animSetClips(set);
        const auto at = [set](Stick stick, Gait gait, TargetKind target = TargetKind::None, std::uint32_t phase = 0) {
            return SquareInput{.target = target,
                               .stick = stick,
                               .gait = gait,
                               .phaseFlags = phase,
                               .heldSet = set,
                               .snapTarget = true};
        };
        // A run: 501 for both buttons; cross also at a sprint, square not.
        CHECK(squareAttack(at({0.0F, 1.0F}, Gait::Run)) == anim_id::kArmedAttackFromRun);
        CHECK(crossAttack(at({0.0F, 1.0F}, Gait::Run)) == anim_id::kArmedAttackFromRun);
        CHECK(crossAttack(at({0.0F, 1.0F}, Gait::Sprint)) == anim_id::kArmedAttackFromRun);
        CHECK(squareAttack(at({0.0F, 1.0F}, Gait::Sprint)) == clips.square);
        // Not with a phase bit: the swing.
        CHECK(squareAttack(at({0.0F, 1.0F}, Gait::Run, TargetKind::None, 0x10000000)) == clips.square);
        // A walk: no walk attack, the standing swing.
        CHECK(squareAttack(at({0.0F, 0.6F}, Gait::Walk)) == clips.square);
        CHECK(crossAttack(at({0.0F, 0.6F}, Gait::Walk)) == clips.cross);
        // No snaps.
        CHECK(squareAttack(at({1.0F, 0.0F}, Gait::Standing)) == clips.square);
        // A grounded or tackled target: the grounded strike (slot 0x13); a grabbed one: the swing, not 120.
        CHECK(squareAttack(at({}, Gait::Standing, TargetKind::Grounded)) == clips.grounded);
        CHECK(squareAttack(at({}, Gait::Standing, TargetKind::Mounted)) == clips.grounded);
        CHECK(crossAttack(at({}, Gait::Standing, TargetKind::Grounded)) == clips.grounded);
        CHECK(squareAttack(at({}, Gait::Standing, TargetKind::Grabbed)) == clips.square);
        // The run comes before the target.
        CHECK(squareAttack(at({0.0F, 1.0F}, Gait::Run, TargetKind::Grounded)) == anim_id::kArmedAttackFromRun);
        // A breakable: the object attack, which objectAttack() picks.
        CHECK(squareAttack(at({}, Gait::Standing, TargetKind::Breakable)) == anim_id::kNone);
    }
}

TEST_CASE("in a fight stance neither unarmed moving attack plays; the armed run attack still does", "[combat]") {
    // Player_Square and Player_Cross skip their run and walk attacks in a stance (combat.md#armed-moves): square goes
    // on to the snap or S1, cross to X1; a bat's run attack 501 has no stance test.
    const auto in = [](Gait gait, int set = 0) {
        return SquareInput{
            .stick = {1.0F, 0.0F}, .gait = gait, .heldSet = set, .fightStance = true, .snapTarget = true};
    };
    CHECK(squareAttack(in(Gait::Run)) == anim_id::kSnapRight);
    CHECK(squareAttack(in(Gait::Jog)) == anim_id::kSnapRight);
    CHECK(crossAttack(in(Gait::Run)) == anim_id::kAttackX1);
    CHECK(crossAttack(in(Gait::Walk)) == anim_id::kAttackX1);
    // The armed run attack does not test the stance.
    CHECK(squareAttack(in(Gait::Run, 3)) == anim_id::kArmedAttackFromRun);
    CHECK(squareAttack(in(Gait::Walk, 3)) == animSetClips(3).square);
}

TEST_CASE("an object to throw in hand: the set's throw from a run at gait 3-5, from a walk at 2, else standing",
          "[combat]") {
    struct Throws {
        int set;
        int standing;
        int walk;
        int run;
    };
    for (const Throws& t : {Throws{4, 505, 506, 507}, Throws{5, 467, 471, 472}, Throws{6, 551, 552, 553}}) {
        INFO("set " << t.set);
        CHECK(throwSet(t.set));
        const auto at = [&t](Gait gait, bool stance = false) {
            return throwAttack(SquareInput{.stick = {}, .gait = gait, .heldSet = t.set, .fightStance = stance});
        };
        CHECK(at(Gait::Standing) == t.standing);
        CHECK(at(Gait::Sneak) == t.standing);
        CHECK(at(Gait::Walk) == t.walk);
        CHECK(at(Gait::Jog) == t.run);
        CHECK(at(Gait::Run) == t.run);
        CHECK(at(Gait::Sprint) == t.run);
        // A fight stance turns only the walking throw into the standing one.
        CHECK(at(Gait::Walk, true) == t.standing);
        CHECK(at(Gait::Run, true) == t.run);
        // The run's phase bits and the stick are not tested.
        CHECK(throwAttack(SquareInput{.stick = {}, .gait = Gait::Run, .phaseFlags = 0x1000000, .heldSet = t.set}) ==
              t.run);
    }
    for (const int set : {0, 1, 2, 3, 7}) {
        CHECK_FALSE(throwSet(set));
        CHECK(throwAttack(SquareInput{.stick = {}, .heldSet = set}) == anim_id::kNone);
    }
}
