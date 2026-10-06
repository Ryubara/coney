// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/lock_pick.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "world_objects/doors.h"

namespace coney::world_objects {

namespace {

constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
// The highest difficulty.
constexpr int kMaxDifficulty = 2;

// The difficulty as an index into the tables.
std::size_t tableIndex(int difficulty) { return static_cast<std::size_t>(difficulty); }

} // namespace

LockPickDial::LockPickDial(int difficulty) : m_difficulty(std::clamp(difficulty, 0, kMaxDifficulty)) { resetPins(); }

void LockPickDial::resetPins() { m_pins.fill(std::numbers::pi_v<float>); }

void LockPickDial::step() {
    if (state() != LockPickState::Running) {
        return;
    }
    const auto pin = static_cast<std::size_t>(currentPin());
    const int direction = kPinDirections.at(tableIndex(m_difficulty)).at(pin);
    float angle = m_pins.at(pin) + (kPinStep * static_cast<float>(kPinSpeeds.at(pin) * direction));
    // Wrapped into [0, 2π).
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0.0F) {
        angle += kTwoPi;
    }
    m_pins.at(pin) = angle;
}

PinPress LockPickDial::press() {
    if (state() != LockPickState::Running) {
        return PinPress::Miss;
    }
    const PinBands& bands = kPinBands.at(tableIndex(m_difficulty));
    const float angle = m_pins.at(static_cast<std::size_t>(currentPin()));
    const bool good = angle >= bands.goodHigh || angle <= bands.goodLow;
    if (!good) {
        m_good = 0;
        m_perfects = 0;
        resetPins();
        return PinPress::Miss;
    }
    const bool perfect = angle >= bands.perfectHigh || angle <= bands.perfectLow;
    ++m_good;
    if (perfect) {
        ++m_perfects;
    }
    return perfect ? PinPress::Perfect : PinPress::Good;
}

void LockPickDial::abandon() { m_good = -1; }

LockPickState LockPickDial::state() const {
    if (m_good < 0) {
        return LockPickState::Abandoned;
    }
    return m_good >= kLockPins ? LockPickState::Succeeded : LockPickState::Running;
}

LockPick::LockPick(const LockPickHandlers& handlers, double human, anim::Vec3 humanAt, double door, int difficulty,
                   ObjectWorld& world)
    : m_handlers(handlers), m_human(human), m_humanAt(humanAt), m_door(door), m_dial(difficulty) {
    if (world.services != nullptr && !m_handlers.start.empty()) {
        world.services->callScript(m_handlers.start, m_human, m_door);
    }
}

PinPress LockPick::press(Doors& doors, ObjectWorld& world) {
    if (m_ended) {
        return PinPress::Miss;
    }
    const PinPress scored = m_dial.press();
    if (scored == PinPress::Miss && world.services != nullptr) {
        world.services->lockPickClick(m_human);
        if (!m_handlers.stageFail.empty()) {
            world.services->callScript(m_handlers.stageFail, m_human, m_door);
        }
    }
    if (m_dial.state() == LockPickState::Succeeded) {
        end(doors, world);
    }
    return scored;
}

void LockPick::abandon(Doors& doors, ObjectWorld& world) {
    if (m_ended) {
        return;
    }
    m_dial.abandon();
    end(doors, world);
}

void LockPick::end(Doors& doors, ObjectWorld& world) {
    m_ended = true;
    ObjectServices* services = world.services;
    if (m_dial.state() == LockPickState::Succeeded) {
        if (services != nullptr && !m_handlers.success.empty()) {
            services->callScript(m_handlers.success, m_human, m_door);
        }
        doors.lockPickSucceeded(m_door, m_humanAt, world);
        // Three perfect presses score the bonus and raise no crime; anything less is a break-in.
        if (services != nullptr) {
            if (m_dial.perfect()) {
                services->scoreEvent(m_human, kPerfectPickCategory, kPerfectPickEvent);
            } else {
                services->reportCrime(kCrimeBreakIn, m_humanAt, m_human);
            }
        }
        return;
    }
    if (services != nullptr && !m_handlers.stop.empty()) {
        services->callScript(m_handlers.stop, m_human, m_door);
    }
    if (doors.lockPickAbandoned(m_door) && services != nullptr) {
        services->reportCrime(kCrimeBreakIn, m_humanAt, m_human);
    }
}

} // namespace coney::world_objects
