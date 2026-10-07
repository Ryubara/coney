// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/block_goal.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "ai/attack_action.h"
#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/fight_goal.h"
#include "combat/ai_counter.h"
#include "combat/anim_ids.h"
#include "combat/attacks.h"

namespace coney::ai {

namespace {

// The record `+0x08` bit that, on the target, ends the block (`0x00228560`).
constexpr std::uint32_t kTargetBusyFlag = 0x400;

// A block's length, and each extension's: a random 1-3 s from now.
std::uint64_t blockEnd(Brain& brain) {
    return brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), kBlockMinMs, kBlockMaxMs));
}

// Whether `brain`'s target aims at its human (its brain's target, or its fighter's for a player).
bool targetAimsHere(const Brain& brain) {
    const Brain* target = brain.target();
    return target != nullptr && (target->target() == &brain || target->human().fighter().target() == &brain.human());
}

// Whether the target is still mid-attack on the human (`0x00228428`: record `+0x08` any of `0x5c0221f`).
bool targetStillAttacking(const Brain& brain) {
    return targetAimsHere(brain) && (brain.target()->human().animator().flags() & kAttackWaitFlags) != 0;
}

// Whether the target is busy or down, which ends the block (`0x00228560`, `0x00228228`). **Coney choice**: Coney's
// humans have no state word, so "any of `0x7bf9e9f7ff0`" is the target not on its feet.
bool targetBusy(const Brain& brain) {
    const human::Human& target = brain.target()->human();
    return (target.animator().flags() & kTargetBusyFlag) != 0 || target.state() != human::TargetState::Standing;
}

// The brain's attack weights with only the punishing kinds left.
AttackWeights punishingWeights(const Brain& brain) {
    AttackWeights weights = brain.attackWeights();
    for (std::size_t kind = 0; kind < weights.size(); ++kind) {
        if (((kPunishingKinds >> kind) & 1U) == 0) {
            weights[kind] = 0;
        }
    }
    return weights;
}

} // namespace

void BlockGoal::start(Brain& brain) {
    m_untilMs = blockEnd(brain);
    m_mayCounter = static_cast<float>(brain.rand100()) < brain.blockChance();
    m_reactionsWereOff = brain.human().fighter().hitReactionsOff();
    if (brain.target() != nullptr) {
        reactionsOff(brain);
    }
}

GoalStatus BlockGoal::process(Brain& brain) {
    human::Fighter& fighter = brain.human().fighter();
    const std::uint32_t flags = brain.human().animator().flags();
    // The hit reactions come back from the sixth update, once the human is free.
    if (++m_updates >= kBlockReactionsBackUpdate && !m_reactionsBack && (flags & kBlockBusyFlags) == 0) {
        m_reactionsBack = true;
        if (!m_reactionsWereOff) {
            fighter.setHitReactionsOff(false);
        }
    }
    // 1. A busy or downed target ends the block.
    if (m_active && brain.target() != nullptr && targetBusy(brain)) {
        brain.press(combat::command::kR1Held);
        m_active = false;
    }
    // 2. Actions queued (a punishing attack): wait for them.
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // 3. Ended: done (an AI is never blocking or ducking).
    if (!m_active) {
        return GoalStatus::Done;
    }
    // 4. The block time run out: extended while the target still attacks the human, else ended.
    const std::uint64_t now = brain.nowMs();
    if (now > m_untilMs) {
        if (targetStillAttacking(brain)) {
            m_untilMs = blockEnd(brain);
            m_patterned = true;
        } else {
            m_active = false;
        }
        brain.press(combat::command::kR1Held);
        return GoalStatus::Stop;
    }
    // 5. The block time runs: in its last second an extended block punishes.
    if (m_untilMs - now < static_cast<std::uint64_t>(kBlockLastMs) && m_patterned &&
        (flags & combat::kPhaseDuck) == 0) {
        if (const std::optional<int> kind = pickAttack(punishingWeights(brain), brain.random()); kind.has_value()) {
            queueAttack(brain, *kind);
        }
        m_active = false;
        return GoalStatus::Stop;
    }
    // Else the counter roll, or R1 held.
    if (brain.target() != nullptr && !m_patterned && counterRoll(brain)) {
        brain.press(combat::command::kR1Pressed);
        reactionsOff(brain);
    } else {
        brain.press(combat::command::kR1Held);
    }
    return GoalStatus::Stop;
}

void BlockGoal::end(Brain& brain) {
    if (!m_reactionsWereOff) {
        brain.human().fighter().setHitReactionsOff(false);
    }
}

bool BlockGoal::counterRoll(Brain& brain) const {
    const bool rolled = static_cast<float>(brain.rand100()) < brain.counterChance();
    return rolled && counterTest(brain);
}

void BlockGoal::reactionsOff(Brain& brain) const {
    if (!m_reactionsWereOff) {
        brain.human().fighter().setHitReactionsOff(true);
    }
}

bool counterTest(const Brain& brain) {
    const human::Human& human = brain.human();
    if ((human.animator().flags() & combat::kAiCounterBusyPhases) != 0 || human.fighter().hurt() ||
        brain.counterChance() <= 0.0F || brain.type() != BrainType::Warrior || !targetAimsHere(brain)) {
        return false;
    }
    const human::Human& target = brain.target()->human();
    if (target.state() == human::TargetState::Grounded ||
        !combat::faceToFace(human.position(), human.heading(), target.position(), target.heading())) {
        return false;
    }
    return combat::aiCounterFor(static_cast<int>(target.animator().animId())) != combat::anim_id::kNone;
}

bool tryBlock(Brain& brain) {
    if (brain.attackWarnings() == 0) {
        return false;
    }
    float chance = brain.blockChance();
    if (brain.type() == BrainType::Warrior) {
        chance /= 4.0F;
    }
    if (static_cast<float>(brain.rand100()) >= chance) {
        return false;
    }
    return brain.pushGoal(std::make_unique<BlockGoal>());
}

} // namespace coney::ai
