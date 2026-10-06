// SPDX-License-Identifier: GPL-3.0-or-later
// A human held in another human's grab or tackle (Holdable): the grabber (Fighter, fighter_grab.cpp) plays the
// victim's side of each paired move on it, aligns it and, once the hold is attached, places it every update at the
// hold's offset (the original's `Human_MoveAttached`, `0x00244e78`). The held human's fighter keeps the hold's state
// (Fighter::enterHold()), which Human::state() reports, holds its movement and keeps it from acting.
// Research: docs/research/combat.md#grab, docs/research/combat.md#grab-posing, docs/research/combat.md#grab-turn
#include <cmath>

#include "human/human.h"

namespace coney::human {

void Human::play(std::span<const std::uint32_t> clips, std::uint32_t loop, AnimState state, TargetState targetState) {
    // The hold's state first: being taken into the hold ends a grab this human held, whose let-go plays on its
    // animator, so the grabber's clips must come after it.
    m_fighter.enterHold(targetState, m_animator, nowMs());
    m_animator.playCombat(clips, loop, state);
}

void Human::playPaired(std::span<const std::uint32_t> clips, const characters::AnimSet& attacker, std::uint32_t loop,
                       AnimState state, TargetState targetState) {
    m_fighter.enterHold(targetState, m_animator, nowMs());
    m_animator.playPaired(clips, attacker, loop, state);
}

void Human::place(anim::Vec3 position, float headingRadians) {
    m_position = position;
    m_heading = wrapAngle(headingRadians);
    m_velocity = anim::Vec3{};
    m_turn = TurnState{};
}

void Human::face(anim::Vec3 point) {
    const anim::Vec3 to = anim::subtract(point, m_position);
    if (std::hypot(to.x, to.y) > 1e-4F) {
        m_heading = headingOf(to);
    }
}

} // namespace coney::human
