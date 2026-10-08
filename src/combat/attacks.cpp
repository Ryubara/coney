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

// The run the run attacks test (`0x00223a60`, and for cross `0x00223a98`): gait 4 (cross: or 5) with the record's +0x08
// clear, and the stick beyond 0.95 (`0x00225c10`). The fight stance is the caller's test: the armed 501 has none.
bool atRun(const SquareInput& input, bool cross) {
    const bool gait = input.gait == human::Gait::Run || (cross && input.gait == human::Gait::Sprint);
    return gait && input.phaseFlags == 0 && input.stick.magnitude() > kSnapStick;
}

// The walk the walk attack tests: gait 1-3 with the stick at 0.12 or more, not in a fight stance.
bool atWalk(const SquareInput& input) {
    return !input.fightStance && walking(input.gait) && input.stick.magnitude() >= kWalkAttackStick;
}

// Whether `set` is a melee weapon's (knife, baton, bat), whose square and cross take the armed branch.
bool meleeSet(int set) { return set >= 1 && set <= 3; }

} // namespace

int snapForStick(Stick stick) {
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

int chainSnap(ChainButton button) {
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

int squareAttack(const SquareInput& input) {
    // A weapon in hand takes its own branch.
    if (meleeSet(input.heldSet)) {
        return armedAttack(input, false);
    }
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
    // Then the gait: a run attack, a walk attack; these come before the snap.
    // The run and walk attacks only outside a fight stance (`0x00228340`).
    if (!input.fightStance && atRun(input, false)) {
        return anim_id::kAttackFromRun;
    }
    if (atWalk(input)) {
        return anim_id::kAttackFromWalk;
    }
    // A snap, with a human on the stick's side that is not the current target, or with `CfgSnap` off for the stick
    // alone; without one square goes on to S1.
    if (!input.snapNeedsTarget || input.snapTarget) {
        if (const int snap = snapForStick(input.stick); snap != anim_id::kNone) {
            return snap;
        }
    }
    return anim_id::kAttackS1;
}

int crossAttack(const SquareInput& input) {
    if (meleeSet(input.heldSet)) {
        return armedAttack(input, true);
    }
    // The moving attacks as square's, the run also at a sprint; no snaps.
    if (!input.fightStance && atRun(input, true)) {
        return anim_id::kAttackFromRun;
    }
    if (atWalk(input)) {
        return anim_id::kAttackFromWalk;
    }
    return anim_id::kAttackX1;
}

bool throwSet(int set) { return set >= 4 && set <= 6; }

int throwAttack(const SquareInput& input) {
    // Gait 3 already counts as a run here; a fight stance turns only the walking throw into the standing one.
    const bool run =
        input.gait == human::Gait::Jog || input.gait == human::Gait::Run || input.gait == human::Gait::Sprint;
    const bool walk = input.gait == human::Gait::Walk && !input.fightStance;
    switch (input.heldSet) {
    case 4:
        return run ? anim_id::kBarrelThrowFromRun : walk ? anim_id::kBarrelThrowFromWalk : anim_id::kBarrelThrow;
    case 5:
        return run    ? anim_id::kOneHandedThrowFromRun
               : walk ? anim_id::kOneHandedThrowFromWalk
                      : anim_id::kOneHandedThrow;
    case 6:
        return run ? anim_id::kGhettoThrowFromRun : walk ? anim_id::kGhettoThrowFromWalk : anim_id::kGhettoThrow;
    default:
        return anim_id::kNone;
    }
}

int armedAttack(const SquareInput& input, bool cross) {
    const AnimSetClips clips = animSetClips(input.heldSet);
    // The run attack first, then the target's state, then the slot's swing.
    if (atRun(input, cross)) {
        return anim_id::kArmedAttackFromRun;
    }
    switch (input.target) {
    case TargetKind::Grounded:
    case TargetKind::Mounted:
        return clips.grounded;
    case TargetKind::Breakable:
        return anim_id::kNone;
    case TargetKind::Grabbed:
    case TargetKind::None:
        break;
    }
    return cross ? clips.cross : clips.square;
}

AnimSetClips animSetClips(int set) {
    switch (set) {
    case 1:
        return AnimSetClips{.square = 45, .cross = 47, .mounting = 49, .grounded = 48};
    case 2:
        return AnimSetClips{.square = 39, .cross = 41, .mounting = 43, .grounded = 42};
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

ChainButton chainButton(CommandId command, Stick stick) {
    if (command == command::kCrossPressed) {
        return ChainButton::Cross;
    }
    // **Coney's choice:** an AI's chain square (0x11) chains as square does; the research has the AI write it for a
    // chain's later steps (docs/research/ai.md) but not where the dispatcher maps it.
    if (command != command::kSquarePressed && command != command::kSquareChain) {
        return ChainButton::None;
    }
    switch (snapForStick(stick)) {
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
    if (next != anim_id::kNone && chainSnap(button) != anim_id::kNone) {
        return chainSnap(button);
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

ChainStep AttackChain::update(ChainButton press, std::uint32_t flags, const CombatTuning& tuning,
                              ChainTargets targets) {
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
        // A snap with nobody in reach (while snaps need a target) plays the plain square step.
        const bool snap = chainSnap(m_buffered) != anim_id::kNone;
        const ChainButton button =
            snap && tuning.snapNeedsTarget && !targets.snapInReach ? ChainButton::Square : m_buffered;
        m_buffered = ChainButton::None;
        // No step from S1 or X1 at a low target.
        if (targets.targetLow && (m_current == anim_id::kAttackS1 || m_current == anim_id::kAttackX1)) {
            return step;
        }
        const int next = nextChainAttack(m_current, button);
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
