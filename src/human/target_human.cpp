// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/target_human.h"

#include <algorithm>
#include <cmath>

#include "animation/anim_task.h"
#include "human/locomotion.h"

namespace coney::human {

namespace {

// The ground clips (docs/references/anim-ids.md): lying on the back and a strike taken there.
constexpr std::uint32_t kGroundedIdle = 196;
constexpr std::uint32_t kGroundedStrikeReact = 195;

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
                         std::span<const anim::Quat, anim::kPoseBones> defaultRotations, int health,
                         anim::Vec3 position, float headingRadians, std::uint32_t seed)
    : m_animator(anims, slots), m_position(position), m_heading(wrapAngle(headingRadians)), m_health(health),
      m_victim(combat::kCivilianPowerClass, seed), m_idle(slots.ids[kSlotIdle]) {
    std::ranges::copy(defaultRotations, m_defaultRotations.begin());
    m_current = capture();
    m_previous = m_current;
}

std::uint64_t TargetHuman::nowMs() const { return m_updates * 1000 / 30; }

void TargetHuman::hit(const TargetHit& hit) { m_victim.hit(hit); }

void TargetHuman::step() {
    ++m_updates;
    // 1. The update's hit lands and is reacted to.
    if (m_victim.pending()) {
        const TargetHit hit = m_victim.takePending();
        if (m_health.apply(hit.damage) > 0) {
            ++m_hits;
        }
        react(hit);
    }
    m_animator.advance(kStepSeconds);
    // A clip that moves the body moves it by its root motion, unless its grabber places it (not by a loop's).
    if (!m_attached && m_state != TargetState::Mounted && m_animator.drivingClipPlaying()) {
        applyRootMotion();
    }
    // 2. The stun's end (357 once the reaction is over) and the rise from the ground.
    if (m_state == TargetState::Standing || m_state == TargetState::Grounded) {
        const bool canRise = m_state == TargetState::Grounded && !m_health.depleted();
        if (m_victim.step(m_animator, m_idle, nowMs(), canRise)) {
            m_state = TargetState::Standing;
        }
    }
    // Between hits it stands in its idle: the controller only finishes the clips it was given.
    m_animator.choose(AnimInputs{});
    m_previous = m_current;
    m_current = capture();
}

void TargetHuman::react(const TargetHit& hit) {
    const VictimFrame frame{.position = m_position, .heading = m_heading, .hurt = hurt(), .flag400 = false};
    // Out of health: a dying clip on its feet (the DIE set half the time), then the ground for good.
    if (m_health.depleted()) {
        if (m_state == TargetState::Standing || m_state == TargetState::Held) {
            m_victim.die(hit, frame, m_animator);
            m_attached = false;
            m_state = TargetState::Grounded;
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
    if (m_victim.react(hit, frame, m_animator, m_idle, nowMs()) == ReactionKind::Knockdown) {
        m_state = TargetState::Grounded;
    }
}

void TargetHuman::play(std::span<const std::uint32_t> clips, std::uint32_t loop, AnimState state,
                       TargetState targetState) {
    m_animator.playCombat(clips, loop, state);
    enter(targetState);
}

void TargetHuman::playPaired(std::span<const std::uint32_t> clips, const characters::AnimSet& attacker,
                             std::uint32_t loop, AnimState state, TargetState targetState) {
    m_animator.playPaired(clips, attacker, loop, state);
    enter(targetState);
}

void TargetHuman::enter(TargetState targetState) {
    // Going down starts the ground time; anything but a hold lets go of the grabber; a stun ends.
    const bool wasGrounded = m_state == TargetState::Grounded;
    if (targetState != TargetState::Grounded) {
        m_victim.clear();
    } else if (!wasGrounded) {
        m_victim.knockDown(nowMs(), false);
    }
    if (targetState != TargetState::Held) {
        m_attached = false;
    }
    m_state = targetState;
}

void TargetHuman::applyRootMotion() {
    const anim::RootMotion root = anim::rootMotionOf(m_animator.pose(m_defaultRotations));
    // The clip's velocity is in the target's axes (facing +y): turned by the heading into the world's.
    const float c = std::cos(m_heading);
    const float s = std::sin(m_heading);
    const anim::Vec3 world{(root.velocity.x * c) - (root.velocity.y * s), (root.velocity.x * s) + (root.velocity.y * c),
                           0.0F};
    m_position = anim::add(m_position, anim::scale(world, kStepSeconds));
    // The turn is per 1/30 s.
    m_heading = wrapAngle(m_heading + (root.turn * 30.0F * kStepSeconds));
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
    return TargetSnapshot{.feet = m_position, .heading = m_heading, .pose = m_animator.pose(m_defaultRotations)};
}

} // namespace coney::human
