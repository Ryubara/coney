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

LocomotionTuning& locomotionTuning() {
    static LocomotionTuning tuning;
    return tuning;
}

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

float targetSpeed(float magnitude, const Speeds& speeds, bool sprinting) {
    if (magnitude <= locomotionTuning().stickDeadZone) {
        return 0.0F;
    }
    if (magnitude <= locomotionTuning().runThreshold) {
        return speeds.walk;
    }
    return sprinting ? speeds.sprint : speeds.run;
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
        return locomotionTuning().jogTurnDegrees * kDegrees;
    case Gait::Run:
        return locomotionTuning().runTurnDegrees * kDegrees;
    case Gait::Sprint:
        return locomotionTuning().sprintTurnDegrees * kDegrees;
    case Gait::Standing:
    case Gait::Sneak:
    case Gait::Walk:
        break;
    }
    return locomotionTuning().walkTurnDegrees * kDegrees;
}

float aiMaxTurn(Gait gait, int boost, bool wounded) {
    // The AI words of the table at 0x005101b0 (walk and stand 0x005101d0, jog 0x005101c8, run 0x005101c0, sprint
    // 0x005101b8; wounded a quarter of the grab word 0x005101b0).
    constexpr float kWalk = 12.0F;
    constexpr float kJog = 6.0F;
    constexpr float kRun = 4.0F;
    constexpr float kSprint = 2.5F;
    constexpr float kWoundedFactor = 0.25F;
    constexpr float kGrabWord = 1.5F;
    if (wounded) {
        return kGrabWord * kWoundedFactor * kDegrees;
    }
    float degrees = kWalk;
    switch (gait) {
    case Gait::Jog:
        degrees = kJog;
        break;
    case Gait::Run:
        degrees = kRun;
        break;
    case Gait::Sprint:
        degrees = kSprint;
        break;
    case Gait::Standing:
    case Gait::Sneak:
    case Gait::Walk:
        break;
    }
    const auto b = static_cast<float>(boost);
    return (boost >= 0 ? degrees * (b + 1.0F) : degrees / (1.0F - b)) * kDegrees;
}

float aiApproachSpeed(float current, float asked) {
    constexpr float kStep = 1.0F / 30.0F;
    if (current <= 0.0F) {
        return std::min(asked, kAiStartSpeed);
    }
    if (current > asked) {
        return std::max(asked, current - kAiFastAcceleration * kStep);
    }
    const float gentle = current + kAiGentleAcceleration * kStep;
    const float next = asked - gentle > kAiGentleGap ? current + kAiFastAcceleration * kStep : gentle;
    return std::min(asked, next);
}

float stanceTurn() { return locomotionTuning().stanceTurnDegrees * kDegrees; }

void setPlayerTurnRates(std::span<const float> degrees) {
    // The table's player words in the script's order; the first (two special states) has no Coney reader.
    LocomotionTuning& tuning = locomotionTuning();
    const std::array<float*, 6> targets{nullptr,
                                        &tuning.sprintTurnDegrees,
                                        &tuning.runTurnDegrees,
                                        &tuning.jogTurnDegrees,
                                        &tuning.walkTurnDegrees,
                                        &tuning.stanceTurnDegrees};
    for (std::size_t i = 0; i < degrees.size() && i < targets.size(); ++i) {
        // Each value is taken only from 0 up to (not including) 90 degrees.
        if (targets.at(i) != nullptr && degrees[i] >= 0.0F && degrees[i] < 90.0F) {
            *targets.at(i) = degrees[i];
        }
    }
}

void setTurnEase(float easeError, float carry) {
    LocomotionTuning& tuning = locomotionTuning();
    tuning.turnEaseError = easeError;
    tuning.turnCarry = carry;
}

float turnToward(float heading, float target, float limit, TurnState& state) {
    const float error = wrapAngle(target - heading);
    const float magnitude = std::abs(error);
    // The ease reaches the full rate at 2.0 rad of error in play; part of the last step carries over (negatively when
    // the error has swapped sides, which damps an overshoot).
    const LocomotionTuning& tuning = locomotionTuning();
    const float full = std::max(tuning.turnEaseError, 1e-3F);
    const float ease = (1.0F - std::cos(kPi * std::min(magnitude, full) / full)) * 0.5F;
    const bool reversed = (error > 0.0F) != (state.lastError > 0.0F) && state.lastError != 0.0F;
    const float carry = (reversed ? kTurnReverseCarry : tuning.turnCarry) * state.lastStep;
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
    return std::min(target, current + locomotionTuning().acceleration * seconds);
}

bool skids(Gait gait, float speed, const Speeds& speeds, float lastMagnitude, float magnitude,
           anim::Vec3 velocityDirection, anim::Vec3 stickDirection) {
    // "At run speed": the run speed at most the measured speed, exactly; a steady run measured a rounding short of
    // its own speed does not skid (the original's ~1 release in 10 at a run, always after a sprint).
    if ((gait != Gait::Run && gait != Gait::Sprint) || !(speeds.run <= speed) ||
        lastMagnitude <= locomotionTuning().runThreshold) {
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

float leanStep(float lean, float turn, float speed, Gait gait) {
    // The limits by gait: walking (and below), jogging, running, sprinting.
    constexpr std::array<float, 4> kLeanLimit{2.0F, 3.0F, 5.0F, 7.0F};
    constexpr std::array<float, 4> kLeanRate{1.0F, 1.2F, 1.3F, 1.8F};
    std::size_t index = 0;
    if (gait == Gait::Jog) {
        index = 1;
    } else if (gait == Gait::Run) {
        index = 2;
    } else if (gait == Gait::Sprint) {
        index = 3;
    }
    const LocomotionTuning& tuning = locomotionTuning();
    const float factor = (gait == Gait::Walk ? tuning.walkLeanFactor : tuning.leanFactor) * speed;
    const float limit = kLeanLimit.at(index) * kDegrees;
    const float target = std::clamp(turn * factor, -limit, limit);
    const float rate = kLeanRate.at(index) * kDegrees;
    return lean + std::clamp(0.625F * (target - lean), -rate, rate);
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
