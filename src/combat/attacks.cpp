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

AnimSetClips animSetClips(int set) {
    switch (set) {
    case 1:
        return AnimSetClips{.square = 45, .cross = 47, .mounting = 50, .grounded = 49};
    case 2:
        return AnimSetClips{.square = 39, .cross = 41, .mounting = 44, .grounded = 43};
    case 3:
        return AnimSetClips{.square = 34, .cross = 36, .mounting = 38, .grounded = 37};
    default:
        return AnimSetClips{};
    }
}

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
    // **Coney's choice:** an AI's chain square (0x11) chains as square does; the research has the AI write it for a
    // chain's later steps (docs/research/ai.md) but not where the dispatcher maps it.
    if (command != command::kSquarePressed && command != command::kSquareChain) {
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

int attackHitUpdate(int animId, const CombatTuning& tuning) {
    // The hits measured at runtime, updates from the clip's start.
    struct Measured {
        int id, hit;
    };
    static constexpr std::array<Measured, 11> kMeasured{{
        {.id = anim_id::kAttackX1, .hit = 8},
        {.id = anim_id::kAttackSS2, .hit = 4},
        {.id = anim_id::kAttackSSS3, .hit = 7},
        {.id = anim_id::kAttackSSX3, .hit = 7},
        {.id = anim_id::kAttackXX2, .hit = 10},
        {.id = anim_id::kAttackSX2, .hit = 7},
        {.id = anim_id::kAttackXS2, .hit = 9},
        {.id = anim_id::kGrabComboStrike1, .hit = 1},
        {.id = anim_id::kGrabComboStrike2, .hit = 1},
        {.id = anim_id::kGrabComboStrike3, .hit = 1},
        {.id = anim_id::kGrabPower1Strike1, .hit = 0},
    }};
    const auto found = std::ranges::find(kMeasured, animId, &Measured::id);
    return found != kMeasured.end() ? found->hit : tuning.hitUpdate;
}

bool AttackChain::start(int animId, const CombatTuning& tuning) {
    m_hit = attackHitUpdate(animId, tuning);
    m_current = animId;
    m_age = 0;
    ++m_combo;
    m_buffered = ChainButton::None;
    return m_hit == 0;
}

void AttackChain::cancel() {
    m_current = anim_id::kNone;
    m_age = 0;
    m_combo = 0;
    m_buffered = ChainButton::None;
}

ChainStep AttackChain::update(ChainButton press, std::uint32_t flags, const CombatTuning& tuning) {
    ChainStep step;
    if (!active()) {
        return step;
    }
    ++m_age;
    if (m_age == m_hit) {
        step.hit = m_current;
    }
    // The attack's clip has given back its bits: the attack is over and the combo starts again.
    if ((flags & kAttackUnderWay) == 0) {
        cancel();
        step.finished = true;
        return step;
    }

    // Buffer the press when the phase takes one; a later press replaces an earlier one.
    if (press != ChainButton::None && accepts(flags)) {
        m_buffered = press;
    }

    // With the window open, a buffered press plays the next attack, or ends the chain where the table has none.
    if ((flags & kPhaseChainWindow) != 0 && m_buffered != ChainButton::None) {
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
    return step;
}

bool AttackChain::accepts(std::uint32_t flags) const {
    if ((flags & kPhaseChainWindow) != 0) {
        return true;
    }
    return (flags & kPhaseWindUp) != 0 && (m_combo < 2 || (m_combo == 2 && m_current == anim_id::kAttackSS2));
}

} // namespace coney::combat
