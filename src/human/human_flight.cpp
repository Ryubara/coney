// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only A flying human: the debug menu's noclip (docs/guides/debug-menu.md#noclip), with no counterpart in
// the original; it changes nothing while no flight is set.
#include <numbers>
#include <optional>

#include "human/human.h"

namespace coney::human {

void Human::setFlight(std::optional<Flight> flight) {
    const bool starting = flight.has_value() && !m_flight.has_value();
    const bool ending = !flight.has_value() && m_flight.has_value();
    m_flight = flight;
    if (starting) {
        // Whatever held the body or moved it through the air ends, as a placement ends it (spawn()), so the flight
        // starts from a standing human: no pair, climb, jump, fall or landing in progress.
        m_fighter.breakPair();
        endClimb();
        m_jumping = false;
        m_airborne = false;
        m_outOfWorld = false;
        m_landingPending = false;
        m_airborneUpdates = 0;
        m_blockedUpdates = 0;
        m_velocity = anim::Vec3{};
        m_turn = TurnState{};
        m_animator.debugIdle();
    }
    if (ending) {
        // Standing where it is, still: the next state update's ground test drops it from here (no ground within the
        // snap's reach makes it airborne, and gravity takes over).
        m_velocity = anim::Vec3{};
        m_airborneUpdates = 0;
        m_blockedUpdates = 0;
    }
}

void Human::fly() {
    // Only called while flying; the check is what the optional access below relies on.
    if (!m_flight.has_value()) {
        return;
    }
    const Flight& flight = *m_flight;
    // The stick as the locomotion reads it (turned by the camera, the game's dead zone), as a velocity across the
    // ground, plus the climb. No wall, ground, gravity or clip root motion acts on it.
    const float push = m_intent.magnitude > locomotionTuning().stickDeadZone ? m_intent.magnitude : 0.0F;
    const float stickHeading = m_intent.angle - std::numbers::pi_v<float> / 2.0F;
    const anim::Vec3 across = anim::scale(facing(stickHeading), push * flight.speed);
    m_velocity = anim::Vec3{across.x, across.y, flight.climb};
    m_position = anim::add(m_position, anim::scale(m_velocity, m_stepSeconds));
    // He faces where he flies, at once; standing still in the air he keeps his heading.
    if (push > 0.0F) {
        m_heading = wrapAngle(stickHeading);
    }
    m_airborne = false;
    m_airborneUpdates = 0;
    m_lastTurn = 0.0F;
}

} // namespace coney::human
