// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/target_human.h"

#include <algorithm>
#include <cmath>

#include "combat/reactions.h"
#include "human/locomotion.h"

namespace coney::human {

namespace {

// The ground clips (docs/references/anim-ids.md): lying on the back, a strike taken there, and getting up.
constexpr std::uint32_t kGroundedIdle = 196;
constexpr std::uint32_t kGroundedStrikeReact = 195;
constexpr std::uint32_t kGroundedRise = 199;
// The clip event type that knocks a victim down (docs/research/combat.md#reactions).
constexpr std::uint16_t kKnockdownEvent = 7;

// The one clip `clip` as an array.
std::array<std::uint32_t, 1> one(std::uint32_t clip) { return {clip}; }

// No clips: just a loop.
constexpr std::array<std::uint32_t, 0> kNoClips{};

} // namespace

TargetSnapshot interpolate(const TargetSnapshot& previous, const TargetSnapshot& current, float alpha) {
    if (alpha >= 1.0F) {
        return current;
    }
    if (alpha <= 0.0F) {
        return previous;
    }
    return TargetSnapshot{.feet = anim::lerp(previous.feet, current.feet, alpha),
                          .heading =
                              wrapAngle(previous.heading + (wrapAngle(current.heading - previous.heading) * alpha)),
                          .pose = anim::blendPoses(previous.pose, current.pose, alpha)};
}

TargetHuman::TargetHuman(const characters::AnimSet& anims, const AnimSlots& slots,
                         std::span<const anim::Quat, anim::kPoseBones> bindRotations, int health, anim::Vec3 position,
                         float headingRadians, std::uint32_t seed)
    : m_animator(anims, slots), m_position(position), m_heading(wrapAngle(headingRadians)), m_health(health),
      m_random(seed), m_idle(slots.ids[kSlotIdle]) {
    std::ranges::copy(bindRotations, m_bindRotations.begin());
    m_current = capture();
    m_previous = m_current;
}

std::uint64_t TargetHuman::nowMs() const { return m_updates * 1000 / 30; }

void TargetHuman::hit(const TargetHit& hit) {
    // The update keeps its largest hit, as the pending damage does.
    if (!m_hasPending || hit.damage > m_pending.damage) {
        m_pending = hit;
        m_hasPending = true;
    }
}

void TargetHuman::step() {
    ++m_updates;
    // 1. The update's hit lands and is reacted to.
    if (m_hasPending) {
        if (m_health.apply(m_pending.damage) > 0) {
            ++m_hits;
        }
        react(m_pending);
        m_hasPending = false;
    }
    m_animator.advance(kStepSeconds);
    // 2. A stun runs out: 357, then the idle.
    if (m_stunUntilMs != 0 && nowMs() >= m_stunUntilMs) {
        m_stunUntilMs = 0;
        if (m_state == TargetState::Standing) {
            m_animator.playCombat(one(combat::kStunEnd), m_idle, AnimState::Attack);
        }
    }
    // 3. A target down with health left gets up once its ground time has passed.
    if (m_state == TargetState::Grounded && !m_health.depleted() && nowMs() >= m_riseAtMs) {
        play(one(kGroundedRise), m_idle, AnimState::Attack, TargetState::Standing);
    }
    // Between hits it stands in its idle: the controller only finishes the clips it was given.
    m_animator.choose(AnimInputs{});
    m_previous = m_current;
    m_current = capture();
}

void TargetHuman::react(const TargetHit& hit) {
    combat::ReactionInput input;
    input.attackAnim = hit.attackAnim;
    input.code = hit.code;
    input.side = combat::victimSide(m_position, m_heading, hit.attacker);
    input.attackerAbove = hit.attacker.z - m_position.z;
    input.victimHurt = hurt();
    // Out of health: a dying clip on its feet (the DIE set half the time), then the ground for good.
    if (m_health.depleted()) {
        if (m_state == TargetState::Standing || m_state == TargetState::Held) {
            const int dying = combat::deathReaction(input, m_random.coin());
            play(one(static_cast<std::uint32_t>(dying)), kGroundedIdle, AnimState::Hold, TargetState::Grounded);
            m_lastReaction = dying;
        } else if (m_state != TargetState::Grounded) {
            play(kNoClips, kGroundedIdle, AnimState::Hold, TargetState::Grounded);
        }
        return;
    }
    if (!hit.react) {
        return;
    }
    // On the ground a strike gets the ground's reaction.
    if (m_state == TargetState::Grounded) {
        m_animator.playCombat(one(kGroundedStrikeReact), kGroundedIdle, AnimState::Hold);
        return;
    }
    if (m_state != TargetState::Standing) {
        return;
    }
    // On its feet: the table's reaction, a knockdown when its clip has the event, else a stun or a plain reaction.
    const combat::Reaction reaction = combat::hitReaction(input);
    const auto clip = static_cast<std::uint32_t>(reaction.animId);
    m_lastReaction = reaction.animId;
    ++m_reactions;
    if (knocksDown(reaction.animId)) {
        play(one(clip), kGroundedIdle, AnimState::Hold, TargetState::Grounded);
        ++m_knockdowns;
        return;
    }
    if ((hit.flags & combat::kRangeFlagStun) != 0 || stunned()) {
        play(one(clip), static_cast<std::uint32_t>(combat::kStunLoop), AnimState::Hold, TargetState::Standing);
        m_stunUntilMs = nowMs() + static_cast<std::uint64_t>(m_class.stunMs);
        ++m_stuns;
        return;
    }
    play(one(clip), m_idle, AnimState::Attack, TargetState::Standing);
}

bool TargetHuman::knocksDown(int id) const {
    const anim::AnimClip* clip = id >= 0 ? m_animator.anims().clip(static_cast<std::size_t>(id)) : nullptr;
    return clip != nullptr && std::ranges::any_of(clip->events, [](const anim::ClipEvent& event) {
               return event.type == kKnockdownEvent;
           });
}

void TargetHuman::play(std::span<const std::uint32_t> clips, std::uint32_t loop, AnimState state,
                       TargetState targetState) {
    m_animator.playCombat(clips, loop, state);
    // Going down starts the ground time.
    if (targetState == TargetState::Grounded && m_state != TargetState::Grounded) {
        m_riseAtMs = nowMs() + static_cast<std::uint64_t>(m_class.groundMs);
    }
    m_state = targetState;
    m_stunUntilMs = 0;
}

void TargetHuman::place(anim::Vec3 position, float headingRadians) {
    m_position = position;
    m_heading = wrapAngle(headingRadians);
}

void TargetHuman::face(anim::Vec3 point) {
    const anim::Vec3 to = anim::subtract(point, m_position);
    if (std::hypot(to.x, to.y) > 1e-4F) {
        m_heading = headingOf(to);
    }
}

TargetSnapshot TargetHuman::capture() const {
    return TargetSnapshot{.feet = m_position, .heading = m_heading, .pose = m_animator.pose(m_bindRotations)};
}

} // namespace coney::human
