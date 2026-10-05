// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/locomotion.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace coney::human {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegrees = kPi / 180.0F;

} // namespace

float wrapAngle(float radians) {
    float wrapped = std::remainder(radians, 2.0F * kPi); // in [-π, π]
    if (wrapped <= -kPi) {
        wrapped += 2.0F * kPi;
    }
    return wrapped;
}

anim::Vec3 facing(float heading) { return anim::Vec3{-std::sin(heading), std::cos(heading), 0.0F}; }

float headingOf(anim::Vec3 direction) { return std::atan2(-direction.x, direction.y); }

StickIntent stickIntent(float stickX, float stickY, anim::Vec3 cameraForward, bool locked) {
    if (locked) {
        return StickIntent{};
    }
    // Up on the stick is the camera's view direction across the ground, right is that turned a quarter clockwise.
    anim::Vec3 forward{cameraForward.x, cameraForward.y, 0.0F};
    const float length = anim::length(forward);
    forward = length > 1e-6F ? anim::scale(forward, 1.0F / length) : anim::Vec3{0.0F, 1.0F, 0.0F};
    const anim::Vec3 right{forward.y, -forward.x, 0.0F};
    const anim::Vec3 turned = anim::add(anim::scale(right, stickX), anim::scale(forward, stickY));
    return StickIntent{.angle = std::atan2(turned.y, turned.x),
                       .magnitude = std::min(1.0F, std::hypot(turned.x, turned.y))};
}

float targetSpeed(float magnitude, const Speeds& speeds) {
    if (magnitude <= kStickDeadZone) {
        return 0.0F;
    }
    return magnitude > kRunThreshold ? speeds.run : speeds.walk;
}

Gait gaitOfSpeed(float speed, const Speeds& speeds) {
    if (speed < kStandingSpeed) {
        return Gait::Standing;
    }
    const std::array<float, 5> gaitSpeeds{speeds.sneak, speeds.walk, speeds.jog, speeds.run, speeds.sprint};
    std::size_t nearest = 0;
    for (std::size_t i = 1; i < gaitSpeeds.size(); ++i) {
        if (std::abs(speed - gaitSpeeds[i]) < std::abs(speed - gaitSpeeds[nearest])) {
            nearest = i;
        }
    }
    return static_cast<Gait>(static_cast<int>(nearest) + 1);
}

Gait gaitForSpeed(float speed, const Speeds& speeds) {
    if (speed >= speeds.sprint) {
        return Gait::Sprint;
    }
    if (speed >= speeds.run) {
        return Gait::Run;
    }
    if (speed >= speeds.jog) {
        return Gait::Jog;
    }
    if (speed >= speeds.walk) {
        return Gait::Walk;
    }
    return Gait::Standing;
}

float maxTurn(Gait gait) {
    switch (gait) {
    case Gait::Jog:
        return 6.0F * kDegrees;
    case Gait::Run:
        return 4.0F * kDegrees;
    case Gait::Sprint:
        return 2.5F * kDegrees;
    case Gait::Standing:
    case Gait::Sneak:
    case Gait::Walk:
        break;
    }
    return 12.0F * kDegrees;
}

float turnToward(float heading, float target, float limit, TurnState& state) {
    const float error = wrapAngle(target - heading);
    const float magnitude = std::abs(error);
    // The ease reaches the full rate at 1.5 rad of error; part of the last step carries over (negatively when the
    // error has swapped sides, which damps an overshoot).
    const float ease = (1.0F - std::cos(kPi * std::min(magnitude, kTurnEaseError) / kTurnEaseError)) * 0.5F;
    const bool reversed = (error > 0.0F) != (state.lastError > 0.0F) && state.lastError != 0.0F;
    const float carry = (reversed ? kTurnReverseCarry : kTurnCarry) * state.lastStep;
    const float step = std::clamp(limit * ease + carry, 0.0F, limit);
    state.lastError = error;
    if (magnitude <= step) {
        state.lastStep = magnitude;
        return wrapAngle(target);
    }
    state.lastStep = step;
    return wrapAngle(heading + (error > 0.0F ? step : -step));
}

float approachSpeed(float current, float target, float seconds) {
    if (current >= target) {
        return target;
    }
    return std::min(target, current + kAcceleration * seconds);
}

bool skids(Gait gait, float speed, const Speeds& speeds, float lastMagnitude, float magnitude,
           anim::Vec3 velocityDirection, anim::Vec3 stickDirection) {
    if ((gait != Gait::Run && gait != Gait::Sprint) || speed < speeds.run || lastMagnitude <= kRunThreshold) {
        return false;
    }
    return magnitude < kSkidStick || anim::dot(velocityDirection, stickDirection) < kSkidDot;
}

float slopeFactor(float normalZ) {
    if (normalZ >= 0.95F) {
        return 1.0F;
    }
    return std::clamp(0.6F + 0.3F * (normalZ - 0.5F), 0.5F, 1.0F);
}

float gaitBlendForSpeed(float speed, const Speeds& speeds) {
    // A character whose two neighbouring speeds are equal (some share one clip) goes straight to the upper gait.
    const auto share = [](float over, float span) { return span > 1e-6F ? over / span : 1.0F; };
    float value = 0.0F;
    if (speed > speeds.run) {
        value = 2.0F + share(speed - speeds.run, speeds.sprint - speeds.run);
    } else if (speed > speeds.jog) {
        value = 1.0F + share(speed - speeds.jog, speeds.run - speeds.jog);
    } else if (speed > speeds.walk) {
        value = share(speed - speeds.walk, speeds.jog - speeds.walk);
    }
    return std::clamp(value, 0.0F, 3.0F);
}

} // namespace coney::human
