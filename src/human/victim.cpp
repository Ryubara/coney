// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/victim.h"

#include <algorithm>
#include <array>

#include "animation/anim_clip.h"
#include "animation/anim_task.h"
#include "combat/attacks.h"
#include "combat/being_hit.h"

namespace coney::human {

namespace {

// The ground clips (docs/references/anim-ids.md): lying on the back and getting up.
constexpr std::uint32_t kGroundedIdle = 196;
constexpr std::uint32_t kGroundedRise = 199;
// The clip event type that knocks a victim down (docs/research/combat-reactions.md#reactions).
constexpr std::uint16_t kKnockdownEvent = 7;
// The rise holds 0x8000 on the record +0x08 (`Human_PlayRise`), which its recovery event (0x48, frame 14 on the disc)
// replaces with 0x40000: from then on the rise's counter is refused (docs/research/combat-reactions.md#ground-escape).
constexpr HeldFlags kRiseHolds{.held = combat::kPhaseRise, .set = combat::kPhaseRise};
// The ground strike's reaction, which a hit early in a reaction clip plays instead of its own.
constexpr std::uint32_t kGroundedStrike = 195;
// How far into a reaction clip a hit still floors the human with 195 (`Human_PlayReaction`).
constexpr float kRefloorProgress = 0.7F;
// The clip flag that makes a clip a reaction clip (`Anim_HasFlag80`).
constexpr std::uint32_t kReactionClipFlag = 0x80;

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
    input.victimFlag200 = frame.flag200;
    input.victimFlag80 = frame.flag80;
    input.victimHurt = frame.hurt;
    input.attackerFlag200000 = hit.attackerFlag200000;
    return input;
}

bool Victim::inReactionClip(const HumanAnimator& animator) {
    if ((animator.flags() & combat::kPhaseRise) != 0 || animator.animId() == kGroundedRise) {
        return true;
    }
    const anim::AnimClip* clip = animator.clip(animator.animId());
    return clip != nullptr && (clip->flags & kReactionClipFlag) != 0;
}

ReactionKind Victim::react(const IncomingHit& hit, const VictimFrame& frame, HumanAnimator& animator,
                           std::uint32_t idle, std::uint64_t nowMs) {
    // 1. In a reaction clip, as many ground strikes as the class takes: no reaction at all.
    const bool inReaction = inReactionClip(animator);
    if (inReaction && m_groundStrikes >= m_class.groundStrikeCap) {
        return ReactionKind::Ignored;
    }
    // 2. Early in it (getting up, say), the hit floors him again with 195, counted as a ground strike. Human_KnockDown
    // is asked for no new stun (an anim above 295 only), but a stun already running lasts until the new rise + the
    // stun time (Human_SetDownTimers, 0x0022f4f8).
    const anim::AnimTask* top = animator.tasks().top();
    if (inReaction && top != nullptr && top->normalisedTime() < kRefloorProgress) {
        m_lastReaction = static_cast<int>(kGroundedStrike);
        ++m_reactions;
        ++m_groundStrikes;
        animator.playCombat(one(kGroundedStrike), kGroundedIdle, AnimState::Hold);
        knockDown(nowMs, stunned());
        ++m_knockdowns;
        return ReactionKind::Knockdown;
    }
    // 3. The hit's own reaction.
    const combat::Reaction reaction = combat::hitReaction(reactionInput(hit, frame));
    const auto clip = static_cast<std::uint32_t>(reaction.animId);
    m_lastReaction = reaction.animId;
    ++m_reactions;
    // A victim with flag 0x100 is never stunned; otherwise the hit's stun flag stuns him, or a stun already running.
    const bool stunFlag = (hit.flags & combat::kRangeFlagStun) != 0;
    // A reaction with a knockdown event lays it down; a stunned one stays stunned until after the rise. Hard to stun
    // plays no part here: Human_KnockDown (0x0022f100) has its own stun rule and never reads that flag.
    if (knocksDown(animator, reaction.animId)) {
        animator.playCombat(one(clip), kGroundedIdle, AnimState::Hold);
        knockDown(nowMs, !frame.unstunnable && (stunFlag || stunned()));
        ++m_knockdowns;
        return ReactionKind::Knockdown;
    }
    // On his feet, a hard-to-stun victim (0x1000000000) is stunned only when already stunned (Human_PlayReaction).
    // **Coney stand-in**: no caller asks for a stun (`Human_PlayReaction`'s last argument).
    const bool stuns = !frame.unstunnable && ((stunFlag && !frame.hardToStun) || stunned());
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
    // 2. Up with the rise over, its ground strikes are forgotten (Human_EndDownState at the rise's end).
    if (!m_grounded && (animator.flags() & combat::kPhaseRise) == 0 && animator.animId() != kGroundedRise) {
        m_groundStrikes = 0;
    }
    // 3. Down long enough, it gets up: into the stun's loop while stunned, else the idle.
    if (m_grounded && canRise && nowMs >= m_riseAtMs) {
        m_grounded = false;
        if (stunned()) {
            animator.playCombat(one(kGroundedRise), static_cast<std::uint32_t>(combat::kStunLoop), AnimState::Hold,
                                kCombatFade, kRiseHolds);
        } else {
            animator.playCombat(one(kGroundedRise), idle, AnimState::Attack, kCombatFade, kRiseHolds);
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
    const anim::AnimClip* clip = id >= 0 ? animator.clip(static_cast<std::uint32_t>(id)) : nullptr;
    return clip != nullptr && std::ranges::any_of(clip->events, [](const anim::ClipEvent& event) {
               return event.type == kKnockdownEvent;
           });
}

} // namespace coney::human
