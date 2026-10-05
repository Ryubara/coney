// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/victim.h"

#include <algorithm>
#include <array>

#include "animation/anim_clip.h"
#include "combat/being_hit.h"

namespace coney::human {

namespace {

// The ground clips (docs/references/anim-ids.md): lying on the back and getting up.
constexpr std::uint32_t kGroundedIdle = 196;
constexpr std::uint32_t kGroundedRise = 199;
// The clip event type that knocks a victim down (docs/research/combat.md#reactions).
constexpr std::uint16_t kKnockdownEvent = 7;

// The one clip `clip` as an array.
std::array<std::uint32_t, 1> one(std::uint32_t clip) { return {clip}; }

} // namespace

Victim::Victim(const combat::PowerClass& powerClass, std::uint32_t seed) : m_class(powerClass), m_random(seed) {}

void Victim::hit(const IncomingHit& hit) {
    if (!m_hasPending || hit.damage > m_pending.damage) {
        m_pending = hit;
        m_hasPending = true;
    }
}

IncomingHit Victim::takePending() {
    m_hasPending = false;
    return m_pending;
}

combat::ReactionInput Victim::reactionInput(const IncomingHit& hit, const VictimFrame& frame) {
    combat::ReactionInput input;
    input.attackAnim = hit.attackAnim;
    input.code = hit.code;
    input.side = combat::victimSide(frame.position, frame.heading, hit.attacker);
    input.attackerAbove = hit.attacker.z - frame.position.z;
    input.victimFlag400 = frame.flag400;
    input.victimHurt = frame.hurt;
    input.attackerFlag200000 = hit.attackerFlag200000;
    return input;
}

ReactionKind Victim::react(const IncomingHit& hit, const VictimFrame& frame, HumanAnimator& animator,
                           std::uint32_t idle, std::uint64_t nowMs) {
    const combat::Reaction reaction = combat::hitReaction(reactionInput(hit, frame));
    const auto clip = static_cast<std::uint32_t>(reaction.animId);
    m_lastReaction = reaction.animId;
    ++m_reactions;
    const bool stuns = (hit.flags & combat::kRangeFlagStun) != 0 || stunned();
    // A reaction with a knockdown event lays it down; a stunned one stays stunned until after the rise.
    if (knocksDown(animator, reaction.animId)) {
        animator.playCombat(one(clip), kGroundedIdle, AnimState::Hold);
        knockDown(nowMs, stuns);
        ++m_knockdowns;
        return ReactionKind::Knockdown;
    }
    if (stuns) {
        animator.playCombat(one(clip), static_cast<std::uint32_t>(combat::kStunLoop), AnimState::Hold);
        stun(nowMs);
        ++m_stuns;
        return ReactionKind::Stun;
    }
    animator.playCombat(one(clip), idle, AnimState::Attack);
    return ReactionKind::Plain;
}

void Victim::die(const IncomingHit& hit, const VictimFrame& frame, HumanAnimator& animator) {
    const int dying = combat::deathReaction(reactionInput(hit, frame), m_random.coin());
    animator.playCombat(one(static_cast<std::uint32_t>(dying)), kGroundedIdle, AnimState::Hold);
    m_lastReaction = dying;
    m_grounded = true;
    m_stunUntilMs = 0;
    m_stunExitPending = false;
}

void Victim::knockDown(std::uint64_t nowMs, bool stunned) {
    m_grounded = true;
    m_riseAtMs = nowMs + static_cast<std::uint64_t>(m_class.groundMs);
    m_stunExitPending = false;
    m_stunUntilMs = stunned ? m_riseAtMs + static_cast<std::uint64_t>(m_class.stunMs) : 0;
}

void Victim::stun(std::uint64_t nowMs) {
    m_stunUntilMs = nowMs + static_cast<std::uint64_t>(m_class.stunMs);
    m_stunExitPending = false;
}

void Victim::clear() {
    m_grounded = false;
    m_stunUntilMs = 0;
    m_stunExitPending = false;
}

bool Victim::step(HumanAnimator& animator, std::uint32_t idle, std::uint64_t nowMs, bool canRise) {
    // 1. The stun's time passes; its exit waits for the clip playing (a reaction, the rise) to end.
    if (m_stunUntilMs != 0 && nowMs >= m_stunUntilMs) {
        m_stunUntilMs = 0;
        m_stunExitPending = true;
    }
    if (m_stunExitPending && !m_grounded && !animator.drivingClipPlaying()) {
        m_stunExitPending = false;
        animator.playCombat(one(static_cast<std::uint32_t>(combat::kStunEnd)), idle, AnimState::Attack);
    }
    // 2. Down long enough, it gets up: into the stun's loop while stunned, else the idle.
    if (m_grounded && canRise && nowMs >= m_riseAtMs) {
        m_grounded = false;
        if (stunned()) {
            animator.playCombat(one(kGroundedRise), static_cast<std::uint32_t>(combat::kStunLoop), AnimState::Hold);
        } else {
            animator.playCombat(one(kGroundedRise), idle, AnimState::Attack);
        }
        return true;
    }
    return false;
}

bool Victim::mash(const HumanAnimator& animator, std::uint64_t nowMs) {
    // Only lying in the ground's loop: a press during the knockdown's reaction does nothing.
    if (!m_grounded || animator.drivingClipPlaying() || nowMs >= m_riseAtMs) {
        return false;
    }
    const auto cut = static_cast<std::uint64_t>(combat::mashCut(m_class.groundMs, m_random));
    const std::uint64_t left = m_riseAtMs - nowMs;
    const std::uint64_t taken = std::min(cut, left);
    m_riseAtMs -= taken;
    if (m_stunUntilMs != 0) {
        m_stunUntilMs -= taken;
    }
    return true;
}

bool Victim::knocksDown(const HumanAnimator& animator, int id) {
    const anim::AnimClip* clip = id >= 0 ? animator.anims().clip(static_cast<std::size_t>(id)) : nullptr;
    return clip != nullptr && std::ranges::any_of(clip->events, [](const anim::ClipEvent& event) {
               return event.type == kKnockdownEvent;
           });
}

} // namespace coney::human
