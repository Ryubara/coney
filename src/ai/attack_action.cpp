// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_action.h"

#include "ai/attack_kinds.h"
#include "ai/brain.h"

namespace coney::ai {

namespace {

// The snap's kind: its press comes with a full stick (the action's angle).
constexpr int kSnapKind = 10;

// Whether `target`'s human has `brain`'s human as its own target (its brain's, or its fighter's for a player).
bool targetsBack(Brain& target, Brain& brain) {
    return target.target() == &brain || target.human().fighter().target() == &brain.human();
}

} // namespace

ActionStatus AttackAction::start(Brain& brain) {
    Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target)) {
        return ActionStatus::Done;
    }
    // The pacing: this brain's next attack, and when the target may be attacked again.
    const bool targetDown = target->human().state() == human::TargetState::Grounded;
    const bool halved = targetsBack(*target, brain) || brain.type() == BrainType::Warrior;
    const std::uint64_t now = brain.nowMs();
    brain.setNextAttackMs(now + static_cast<std::uint64_t>(brain.attackDelayMs(m_kind, targetDown, halved)));
    if (m_kind >= 0 && m_kind < static_cast<int>(kAttackKinds)) {
        const int perKind = brain.settings().attackDelaysMs[static_cast<std::size_t>(m_kind)];
        target->setAttackableAtMs(now + static_cast<std::uint64_t>(perKind > 0 ? perKind : 0));
    }
    // The press, once.
    m_command = commandOf(m_kind, brain.random());
    if (m_kind == kSnapKind) {
        brain.writeStick(anim::subtract(target->human().position(), brain.human().position()), 1.0F);
        m_stick = true;
    }
    brain.press(m_command);
    return ActionStatus::Running;
}

ActionStatus AttackAction::update(Brain& brain) {
    if ((brain.human().animator().flags() & kAttackWaitFlags) != 0) {
        return ActionStatus::Running;
    }
    if (m_stick) {
        brain.releaseStick();
        m_stick = false;
    }
    return ActionStatus::Done;
}

bool AttackAction::abort(Brain& brain) {
    if ((brain.human().animator().flags() & kAttackWaitFlags) != 0) {
        return false;
    }
    if (m_stick) {
        brain.releaseStick();
    }
    return true;
}

} // namespace coney::ai
