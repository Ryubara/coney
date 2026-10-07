// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_action.h"

#include <algorithm>
#include <memory>

#include "ai/attack_kinds.h"
#include "ai/attack_places.h"
#include "ai/brain.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"

namespace coney::ai {

namespace {

// The snap's kind: its press comes with a full stick toward the target when the action has no angle of its own.
constexpr int kSnapKind = 10;

// Whether `brain`'s human is busy (`Human_IsBusy`'s record bits).
bool busy(Brain& brain) { return (brain.human().animator().flags() & human::kBusyFlags) != 0; }

// Whether `target`'s human has `brain`'s human as its own target (its brain's, or its fighter's for a player).
bool targetsBack(Brain& target, Brain& brain) {
    return target.target() == &brain || target.human().fighter().target() == &brain.human();
}

} // namespace

std::unique_ptr<AttackAction> AttackAction::onSelf(int kind, std::int16_t delayMs) {
    auto action = std::make_unique<AttackAction>(kind, delayMs);
    action->m_self = true;
    return action;
}

ActionStatus AttackAction::start(Brain& brain) {
    // On himself: only the press.
    if (m_self) {
        m_command = commandOf(m_kind, brain.random());
        brain.press(m_command);
        return ActionStatus::Running;
    }
    Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target)) {
        return ActionStatus::Done;
    }
    // The pacing: this brain's next attack.
    const bool targetDown = target->human().state() == human::TargetState::Grounded;
    const bool halved = targetsBack(*target, brain) || brain.type() == BrainType::Warrior;
    const std::uint64_t now = brain.nowMs();
    brain.setNextAttackMs(now + static_cast<std::uint64_t>(brain.attackDelayMs(m_kind, targetDown, halved)));
    // When the target may be attacked again: the swing shared among those his spacing lets swing at once.
    if (!busy(brain) && !busy(*target)) {
        const bool downOrOut = targetDown || target->human().fighter().victim().grounded();
        const int swing = swingTimeMs(m_kind, targetDown, brain.human().anims());
        const int gap = attackableGapMs(swing, target->fightBook().spacing.inUse(downOrOut));
        target->setAttackableAtMs(std::max(target->attackableAtMs(), now) + static_cast<std::uint64_t>(gap));
    }
    // The press, once, with its stick.
    m_command = commandOf(m_kind, brain.random());
    if (m_stickHeading.has_value()) {
        brain.writeStick(human::facing(*m_stickHeading), 1.0F);
        m_stick = true;
    } else if (m_kind == kSnapKind) {
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
