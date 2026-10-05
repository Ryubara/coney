// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/attacks.h"

namespace coney::combat {

namespace {

// Gaits 1 to 3: sneak, walk and jog, where square attacks from a walk.
bool walking(human::Gait gait) {
    return gait == human::Gait::Sneak || gait == human::Gait::Walk || gait == human::Gait::Jog;
}

// The snap attack of a stick beyond kSnapStick pointing off the front, or anim_id::kNone.
int snapAttack(Stick stick) {
    if (stick.magnitude() <= kSnapStick) {
        return anim_id::kNone;
    }
    switch (sideOf(stick.angleDegrees())) {
    case Side::Right:
        return anim_id::kSnapRight;
    case Side::Left:
        return anim_id::kSnapLeft;
    case Side::Rear:
        return anim_id::kSnapBack;
    case Side::Front:
        break;
    }
    return anim_id::kNone;
}

// The snap attack a buffered snap plays.
int snapOf(ChainButton button) {
    switch (button) {
    case ChainButton::SnapRight:
        return anim_id::kSnapRight;
    case ChainButton::SnapLeft:
        return anim_id::kSnapLeft;
    case ChainButton::SnapBack:
        return anim_id::kSnapBack;
    default:
        return anim_id::kNone;
    }
}

} // namespace

int squareAttack(const SquareInput& input) {
    // The target's state first.
    switch (input.target) {
    case TargetKind::Grounded:
        return anim_id::kGroundedStrike1;
    case TargetKind::Mounted:
        return anim_id::kMountingStrike;
    case TargetKind::Grabbed:
        return anim_id::kGrabFrontStrike;
    case TargetKind::Breakable:
        return anim_id::kNone;
    case TargetKind::None:
        break;
    }
    // Then the stick: a snap, a run attack, a walk attack.
    if (input.snapAttacks) {
        if (const int snap = snapAttack(input.stick); snap != anim_id::kNone) {
            return snap;
        }
    }
    const float length = input.stick.magnitude();
    if (input.gait == human::Gait::Run && input.phaseFlags == 0 && length > kSnapStick) {
        return anim_id::kAttackFromRun;
    }
    if (walking(input.gait) && length >= kWalkAttackStick) {
        return anim_id::kAttackFromWalk;
    }
    return anim_id::kAttackS1;
}

int crossAttack() { return anim_id::kAttackX1; }

int objectAttack(float height) {
    if (height < 0.0F) {
        return anim_id::kGroundedStrike2;
    }
    return height <= kObjectLowHeight ? anim_id::kBreakObjectLow : anim_id::kBreakObjectMid;
}

bool runningAttackAllowed(human::Gait gait, std::uint32_t phaseFlags) {
    return (gait == human::Gait::Run && phaseFlags == 0) || gait == human::Gait::Sprint;
}

ChainButton chainButton(CommandId command, Stick stick, bool snapAttacks) {
    if (command == command::kCrossPressed) {
        return ChainButton::Cross;
    }
    if (command != command::kSquarePressed) {
        return ChainButton::None;
    }
    switch (snapAttacks ? snapAttack(stick) : anim_id::kNone) {
    case anim_id::kSnapRight:
        return ChainButton::SnapRight;
    case anim_id::kSnapLeft:
        return ChainButton::SnapLeft;
    case anim_id::kSnapBack:
        return ChainButton::SnapBack;
    default:
        return ChainButton::Square;
    }
}

int nextChainAttack(int current, ChainButton button) {
    // A snap continues wherever a square would.
    const bool square = button != ChainButton::Cross && button != ChainButton::None;
    const bool cross = button == ChainButton::Cross;
    int next = anim_id::kNone;
    switch (current) {
    case anim_id::kAttackS1:
        next = square ? anim_id::kAttackSS2 : (cross ? anim_id::kAttackSX2 : anim_id::kNone);
        break;
    case anim_id::kAttackX1:
        next = square ? anim_id::kAttackXS2 : (cross ? anim_id::kAttackXX2 : anim_id::kNone);
        break;
    case anim_id::kAttackSS2:
        next = square ? anim_id::kAttackSSS3 : (cross ? anim_id::kAttackSSX3 : anim_id::kNone);
        break;
    default:
        break;
    }
    if (next != anim_id::kNone && snapOf(button) != anim_id::kNone) {
        return snapOf(button);
    }
    return next;
}

void AttackChain::start(int animId) {
    m_current = animId;
    m_age = 0;
    ++m_combo;
    m_buffered = ChainButton::None;
}

void AttackChain::cancel() {
    m_current = anim_id::kNone;
    m_age = 0;
    m_combo = 0;
    m_buffered = ChainButton::None;
}

ChainStep AttackChain::update(ChainButton press, const CombatTuning& tuning) {
    ChainStep step;
    if (!active()) {
        return step;
    }
    ++m_age;

    // Buffer the press when the phase takes one; a later press replaces an earlier one.
    if (press != ChainButton::None && accepts(tuning)) {
        m_buffered = press;
    }
    if (m_age == tuning.hitUpdate) {
        step.hit = m_current;
    }

    // With the window open, a buffered press plays the next attack, or ends the chain where the table has none.
    if ((phaseFlags(tuning) & kPhaseChainWindow) != 0 && m_buffered != ChainButton::None) {
        const int next = nextChainAttack(m_current, m_buffered);
        m_buffered = ChainButton::None;
        if (next != anim_id::kNone) {
            start(next);
            step.started = next;
            return step;
        }
    }

    // The attack ran out with nothing after it: the fight idle returns and the combo starts again.
    if (m_age >= tuning.attackEndUpdate) {
        cancel();
        step.finished = true;
    }
    return step;
}

std::uint32_t AttackChain::phaseFlags(const CombatTuning& tuning) const {
    if (!active()) {
        return 0;
    }
    if (m_age < tuning.chainOpenUpdate) {
        return kPhaseWindUp;
    }
    if (m_age < tuning.chainCloseUpdate) {
        return kPhaseChainWindow;
    }
    if (m_age < tuning.recoveryUpdate) {
        return kPhaseEnd;
    }
    return kPhaseRecovery;
}

bool AttackChain::accepts(const CombatTuning& tuning) const {
    const std::uint32_t phase = phaseFlags(tuning);
    if (phase == kPhaseChainWindow) {
        return true;
    }
    return phase == kPhaseWindUp && (m_combo < 2 || (m_combo == 2 && m_current == anim_id::kAttackSS2));
}

} // namespace coney::combat
