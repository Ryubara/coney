// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/fighter.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "combat/ai_counter.h"
#include "combat/anim_ids.h"
#include "combat/reactions.h"
#include "human/fighter_clips.h"

// The AI's counter (command 3) to a grab or a tackle coming at it, as a paired move: the victim of the intro presses
// the command (its block goal's roll, ai::BlockGoal), and the human in the intro plays the pair on its own update.
// Research: docs/research/ai.md#block, docs/research/combat-moves.md#counters

namespace coney::human {

std::optional<CounterPress> Fighter::takeCounterPress() {
    if (!std::exchange(m_counterPressed, false)) {
        return std::nullopt;
    }
    const auto damageOf = [this](int animId) {
        return m_ranges != nullptr ? m_ranges->damage(static_cast<std::size_t>(animId)) : 0;
    };
    return CounterPress{.grabDamage = damageOf(combat::kAiGrabCounter),
                        .tackleDamage = damageOf(combat::kAiTackleCounter)};
}

bool Fighter::answerCounter(const FighterInput& input, HumanAnimator& animator, float heading) {
    // Only during this human's own grab or tackle intro (the clip the counterer tests, 69-71 or 2-4).
    if (m_held == nullptr || (m_pair != PairStage::Intro && !m_tacklePending)) {
        return false;
    }
    const int counter = combat::aiCounterFor(static_cast<int>(animator.animId()));
    if (counter == combat::anim_id::kNone) {
        return false;
    }
    Holdable& victim = *m_held;
    const std::optional<CounterPress> press = victim.takeCounterPress();
    // Face to face (inferred on the page) and the attacker not on the ground.
    if (!press.has_value() || m_victim.grounded() ||
        !combat::faceToFace(input.position, heading, victim.position(), victim.heading())) {
        return false;
    }
    // The pair: the hold ends with no clip of its own, the victim plays the counter from its set and this human the
    // reaction from the victim's set (`Attack_StartPaired`), takes the counter's damage and stands stunned after it.
    // **Coney's reading**: the stun follows the grab counter's runtime (76 / 77, then 355-357); the tackle counter
    // (9 / 10) is taken to end alike.
    const bool tackle = counter == combat::kAiTackleCounter;
    const int damage = tackle ? press->tackleDamage : press->grabDamage;
    const std::uint32_t clip = clips::clipOf(counter);
    releaseHold(animator, false);
    victim.face(input.position);
    victim.play(clips::one(clip), clips::kIdle, AnimState::Attack, TargetState::Standing);
    animator.playPaired(clips::one(clip + 1), victim.anims(), static_cast<std::uint32_t>(combat::kStunLoop),
                        AnimState::Hold);
    if (damage > 0) {
        m_victim.hit(
            IncomingHit{.damage = damage, .attackAnim = counter, .attacker = victim.position(), .react = false});
    }
    m_victim.stun(input.nowMs);
    m_reacting = true;
    return true;
}

} // namespace coney::human
