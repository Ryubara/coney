// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/attacks.h"

#include <algorithm>
#include <array>

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

AttackTiming attackTiming(int animId, const CombatTuning& tuning) {
    // A measured attack, by hit, window opening, end phase and idle; -1 where the research has no number.
    struct Measured {
        int id, hit, open, close, recovery, end;
    };
    static constexpr std::array<Measured, 15> kMeasured{{
        {.id = anim_id::kAttackX1, .hit = 8, .open = 10, .close = 20, .recovery = 21, .end = 30},
        {.id = anim_id::kAttackSS2, .hit = 4, .open = 6, .close = -1, .recovery = -1, .end = -1},
        {.id = anim_id::kAttackSSS3, .hit = 7, .open = -1, .close = 16, .recovery = -1, .end = 26},
        {.id = anim_id::kAttackSSX3, .hit = 7, .open = -1, .close = 24, .recovery = -1, .end = 30},
        {.id = anim_id::kAttackXX2, .hit = 10, .open = -1, .close = 17, .recovery = 19, .end = 30},
        {.id = anim_id::kAttackSX2, .hit = 7, .open = -1, .close = 14, .recovery = -1, .end = 19},
        {.id = anim_id::kAttackXS2, .hit = 9, .open = -1, .close = 22, .recovery = -1, .end = 30},
        {.id = anim_id::kGrabComboStrike1, .hit = 1, .open = -1, .close = -1, .recovery = -1, .end = 21},
        {.id = anim_id::kGrabComboStrike2, .hit = 1, .open = -1, .close = -1, .recovery = -1, .end = 21},
        {.id = anim_id::kGrabComboStrike3, .hit = 1, .open = -1, .close = -1, .recovery = -1, .end = 23},
        {.id = anim_id::kGrabPower1Strike1, .hit = 0, .open = 19, .close = 33, .recovery = -1, .end = 44},
        {.id = anim_id::kAttackFromWalk, .hit = -1, .open = -1, .close = -1, .recovery = -1, .end = 24},
        {.id = anim_id::kAttackFromRun, .hit = -1, .open = -1, .close = -1, .recovery = -1, .end = 21},
        {.id = anim_id::kRunningAttackCharge, .hit = -1, .open = -1, .close = -1, .recovery = -1, .end = 27},
        {.id = anim_id::kRunningAttackDive, .hit = -1, .open = -1, .close = -1, .recovery = -1, .end = 60},
    }};
    // S1's timing, the tunable one, is every other attack's.
    AttackTiming timing{.hit = tuning.hitUpdate,
                        .chainOpen = tuning.chainOpenUpdate,
                        .chainClose = tuning.chainCloseUpdate,
                        .recovery = tuning.recoveryUpdate,
                        .end = tuning.attackEndUpdate};
    const auto found = std::ranges::find(kMeasured, animId, &Measured::id);
    if (found == kMeasured.end()) {
        return timing;
    }
    // The measured columns, the unmeasured ones filled as attackTiming() documents.
    if (found->hit >= 0) {
        timing.hit = found->hit;
    }
    if (found->end >= 0) {
        timing.end = found->end;
        timing.recovery = found->end - (tuning.attackEndUpdate - tuning.recoveryUpdate);
    }
    if (found->recovery >= 0) {
        timing.recovery = found->recovery;
    }
    if (found->close >= 0) {
        timing.chainClose = found->close;
    } else if (found->open < 0) {
        timing.chainClose = timing.recovery;
    }
    timing.chainOpen = found->open >= 0 ? found->open : timing.chainClose;
    timing.recovery = std::max(timing.recovery, timing.chainClose);
    return timing;
}

bool AttackChain::start(int animId, const CombatTuning& tuning) {
    m_timing = attackTiming(animId, tuning);
    m_current = animId;
    m_age = 0;
    ++m_combo;
    m_buffered = ChainButton::None;
    return m_timing.hit == 0;
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
    if (press != ChainButton::None && accepts()) {
        m_buffered = press;
    }
    if (m_age == m_timing.hit) {
        step.hit = m_current;
    }

    // With the window open, a buffered press plays the next attack, or ends the chain where the table has none.
    if ((phaseFlags() & kPhaseChainWindow) != 0 && m_buffered != ChainButton::None) {
        const int next = nextChainAttack(m_current, m_buffered);
        m_buffered = ChainButton::None;
        if (next != anim_id::kNone) {
            if (start(next, tuning)) {
                step.hit = next;
            }
            step.started = next;
            return step;
        }
    }

    // The attack ran out with nothing after it: the fight idle returns and the combo starts again.
    if (m_age >= m_timing.end) {
        cancel();
        step.finished = true;
    }
    return step;
}

std::uint32_t AttackChain::phaseFlags() const {
    if (!active()) {
        return 0;
    }
    // The run attack carries its own bit for its whole length, not the chain's phases.
    if (m_current == anim_id::kAttackFromRun) {
        return kPhaseRunAttack;
    }
    if (m_age < m_timing.chainOpen) {
        return kPhaseWindUp;
    }
    if (m_age < m_timing.chainClose) {
        return kPhaseChainWindow;
    }
    if (m_age < m_timing.recovery) {
        return kPhaseEnd;
    }
    return kPhaseRecovery;
}

bool AttackChain::accepts() const {
    const std::uint32_t phase = phaseFlags();
    if (phase == kPhaseChainWindow) {
        return true;
    }
    return phase == kPhaseWindUp && (m_combo < 2 || (m_combo == 2 && m_current == anim_id::kAttackSS2));
}

} // namespace coney::combat
