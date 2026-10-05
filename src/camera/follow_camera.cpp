// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/follow_camera.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace coney::camera {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kRadians = kPi / 180.0F;
// The right stick's yaw: 150°/s at the end of the travel, 60°/s at the edge of its ±48 dead zone.
constexpr float kYawFast = 2.618F;
constexpr float kYawSpan = kPi / 2.0F;
constexpr std::uint8_t kYawLeftEdge = 64;
constexpr std::uint8_t kYawRightEdge = 176;
// The right stick's pitch: 85°/s at the end, 55°/s at the edge, only near the ends of the travel.
constexpr float kPitchFast = 85.0F * kRadians;
constexpr float kPitchSlow = 55.0F * kRadians;
constexpr std::uint8_t kPitchUpEdge = 8;
constexpr std::uint8_t kPitchDownEdge = 232;
// The hard band widens the leash band by at least these.
constexpr float kHardNearShare = 0.05F;
constexpr float kHardNearMinimum = 0.2F;
constexpr float kHardFarShare = 0.06F;
constexpr float kHardFarMinimum = 0.35F;
// The auto-centre rule's angles and rates, degrees and degrees a second (docs/research/camera.md#heading).
constexpr float kCentreFrom = 22.5F;
constexpr float kCentreMid = 45.0F;
constexpr float kCentreSlope = 2.444F;
constexpr float kCentreFastFrom = 90.0F;
constexpr float kCentreFastTo = 100.0F;
constexpr float kCentreFast = 200.0F;
constexpr float kCentreBehind = 157.5F;
constexpr float kCentreFallSlope = 2.435F;
constexpr float kCentreSlow = 60.0F;

// The elevation of `offset` above the horizontal, radians.
float elevationOf(anim::Vec3 offset) { return std::atan2(offset.z, std::hypot(offset.x, offset.y)); }

// `offset` moved to `length` along itself; straight back from the look-at point when it has no length.
anim::Vec3 withLength(anim::Vec3 offset, float length) {
    const float current = anim::length(offset);
    return current > 1e-6F ? anim::scale(offset, length / current) : anim::Vec3{0.0F, -length, 0.0F};
}

// The heading (0 facing +y, anticlockwise) of a direction across the ground, as the human's headings are measured.
float headingOfView(anim::Vec3 view) { return std::atan2(-view.x, view.y); }

// The angle wrapped to (-π, π].
float wrapped(float radians) {
    float result = std::remainder(radians, 2.0F * kPi);
    if (result <= -kPi) {
        result += 2.0F * kPi;
    }
    return result;
}

} // namespace

float rightStickYawRate(std::uint8_t rawX) {
    if (rawX <= kYawLeftEdge) {
        return kYawFast - (static_cast<float>(rawX) / static_cast<float>(kYawLeftEdge)) * kYawSpan;
    }
    if (rawX >= kYawRightEdge) {
        const float share = static_cast<float>(rawX - kYawRightEdge) / static_cast<float>(255 - kYawRightEdge);
        return -(kYawFast - kYawSpan + share * kYawSpan);
    }
    return 0.0F;
}

float rightStickPitchRate(std::uint8_t rawY) {
    if (rawY <= kPitchUpEdge) {
        return kPitchFast - (static_cast<float>(rawY) / static_cast<float>(kPitchUpEdge)) * (kPitchFast - kPitchSlow);
    }
    if (rawY >= kPitchDownEdge) {
        const float share = static_cast<float>(rawY - kPitchDownEdge) / static_cast<float>(255 - kPitchDownEdge);
        return -(kPitchSlow + share * (kPitchFast - kPitchSlow));
    }
    return 0.0F;
}

float autoCentreRate(float angle, bool running) {
    const float a = std::abs(angle) / kRadians;
    float rate = 0.0F; // degrees a second
    if (a < kCentreFrom) {
        rate = 0.0F;
    } else if (a < kCentreFastFrom) {
        rate = (a - kCentreMid) * kCentreSlope + kCentreMid;
    } else if (a < kCentreFastTo) {
        rate = kCentreFast;
    } else if (a <= kCentreBehind || running) {
        rate = (kCentreBehind - a) * kCentreFallSlope + kCentreSlow;
    }
    return rate * kRadians;
}

FollowTuning& followTuning() {
    static FollowTuning tuning;
    return tuning;
}

FollowSettings& followDefaults() {
    static FollowSettings settings;
    return settings;
}

FollowCamera::FollowCamera(anim::Vec3 targetFeet, float targetHeading, const FollowSettings& settings)
    : m_settings(settings), m_targetPitch(settings.pitchDegrees * kRadians),
      m_lowerPitch(
          std::max(kLowestPitchDegrees * kRadians, std::atan((1.0F - settings.lookAtHeight) / settings.maxDistance))),
      m_upperPitch(settings.upperPitchDegrees * kRadians), m_lastFeetZ(targetFeet.z) {
    // Behind the target's facing, at the band's near edge, at the target pitch.
    m_lookAt = anim::add(targetFeet, anim::Vec3{0.0F, 0.0F, settings.lookAtHeight});
    const float across = settings.leashNear * std::cos(m_targetPitch);
    const anim::Vec3 behind{std::sin(targetHeading) * across, -std::cos(targetHeading) * across,
                            settings.leashNear * std::sin(m_targetPitch)};
    m_wanted = anim::add(m_lookAt, behind);
    m_position = m_wanted;
    stepHardBand();
}

anim::Vec3 FollowCamera::forward() const {
    const anim::Vec3 view = anim::subtract(m_lookAt, m_position);
    return anim::length(view) > 1e-6F ? anim::normalise(view) : anim::Vec3{0.0F, 1.0F, 0.0F};
}

float FollowCamera::bandNear() const {
    const float share = m_zoom > 0 ? kSprintZoomShare.at(static_cast<std::size_t>(m_zoom - 1)) : 0.0F;
    return m_settings.leashNear + (m_settings.minDistance - m_settings.leashNear) * share;
}

float FollowCamera::bandFar() const { return bandNear() + (m_settings.leashFar - m_settings.leashNear); }

float FollowCamera::targetPitch() const {
    // The sprint zoom takes the target pitch toward the sprint pitch in even steps (0.4286° an update from 13° to 7°).
    const float share = static_cast<float>(std::min(m_zoom, kSprintPitchUpdates)) / kSprintPitchUpdates;
    return m_targetPitch + (m_settings.sprintPitchDegrees * kRadians - m_targetPitch) * share;
}

void FollowCamera::yaw(float angle) {
    const anim::Vec3 offset = anim::subtract(m_wanted, m_lookAt);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    m_wanted = anim::add(m_lookAt, anim::Vec3{offset.x * c - offset.y * s, offset.x * s + offset.y * c, offset.z});
}

void FollowCamera::pitchToward(float maxStep) {
    const anim::Vec3 offset = anim::subtract(m_wanted, m_lookAt);
    const float distance = anim::length(offset);
    const float across = std::hypot(offset.x, offset.y);
    if (distance < 1e-6F) {
        return;
    }
    const float current = elevationOf(offset);
    const float next = std::clamp(targetPitch(), current - maxStep, current + maxStep);
    // Keep the heading across the ground; straight behind +y when the camera is right above the point.
    const float headingX = across > 1e-6F ? offset.x / across : 0.0F;
    const float headingY = across > 1e-6F ? offset.y / across : -1.0F;
    const float newAcross = distance * std::cos(next);
    m_wanted = anim::add(m_lookAt, anim::Vec3{headingX * newAcross, headingY * newAcross, distance * std::sin(next)});
}

void FollowCamera::collide(const raycast::CollisionMesh& mesh) {
    const anim::Vec3 view = anim::subtract(m_position, m_lookAt);
    const float distance = anim::length(view);
    if (distance < 1e-6F) {
        return;
    }
    const anim::Vec3 direction = anim::scale(view, 1.0F / distance);
    const raycast::Ray ray{.origin = raycast::Vec3{m_lookAt.x, m_lookAt.y, m_lookAt.z},
                           .direction = raycast::Vec3{direction.x, direction.y, direction.z},
                           .length = distance};
    if (const auto hit = mesh.rayCast(ray, kSeeThroughMaterials, kCameraRayMask); hit) {
        const float pulled = std::max(followTuning().minCollisionDistance, hit->t - followTuning().collisionMargin);
        m_position = anim::add(m_lookAt, anim::scale(direction, std::min(pulled, distance)));
    }
}

void FollowCamera::followLookAt(anim::Vec3 feet) {
    const float wantedZ = feet.z + m_settings.lookAtHeight;
    // A sudden rise (a climb's move onto a top) starts the ease from where the look-at point was.
    if (!m_heightEasing && feet.z - m_lastFeetZ > kHeightRise) {
        m_heightEasing = true;
        m_heightFinishLeft = 0;
    }
    m_lastFeetZ = feet.z;
    float z = wantedZ;
    if (m_heightEasing) {
        const float gap = wantedZ - m_lookAt.z;
        if (gap <= 0.0F) {
            // Caught up, or the target went down: follow the feet again.
            m_heightEasing = false;
        } else if (m_heightFinishLeft == 0 && gap > kHeightFinish) {
            z = m_lookAt.z + kHeightEase * gap;
        } else {
            // The last part goes in a fixed number of even updates.
            if (m_heightFinishLeft == 0) {
                m_heightFinishLeft = kHeightFinishUpdates;
            }
            z = m_lookAt.z + gap / static_cast<float>(m_heightFinishLeft);
            --m_heightFinishLeft;
            m_heightEasing = m_heightFinishLeft > 0;
        }
    }
    m_lookAt = anim::Vec3{feet.x, feet.y, z};
}

void FollowCamera::autoCentre(anim::Vec3 view, float heading, bool running, float seconds) {
    // The angle between the camera's view across the ground and the target's facing; each update turns by the rule's
    // rate (which may be negative: a small turn away) at most the whole angle.
    if (std::hypot(view.x, view.y) < 1e-6F) {
        return;
    }
    const float difference = wrapped(heading - headingOfView(view));
    const float angle = std::abs(difference);
    const float step = std::min(angle, autoCentreRate(angle, running) * seconds);
    if (step == 0.0F) {
        return;
    }
    m_lastAutoTurn = difference >= 0.0F ? step : -step;
    yaw(m_lastAutoTurn);
}

void FollowCamera::stepSprintZoom(bool sprinting) {
    // In from the first update at the sprint gait; out again once the sprint gait has been gone for a while.
    m_sinceSprint = sprinting ? 0 : m_sinceSprint + 1;
    if (sprinting) {
        m_zoom = std::min(m_zoom + 1, static_cast<int>(kSprintZoomShare.size()));
    } else if (m_sinceSprint > kSprintZoomHold) {
        m_zoom = std::max(m_zoom - 1, 0);
    }
}

void FollowCamera::stepHardBand() {
    // The leash band widened by max(5%, 0.2 m) and max(6%, 0.35 m).
    const float near = bandNear();
    const float far = bandFar();
    const float hardNear = near - std::max(kHardNearShare * near, kHardNearMinimum);
    const float hardFar = far + std::max(kHardFarShare * far, kHardFarMinimum);
    // Widening is at once (and a new camera, whose edges start at the extremes, starts there); shrinking eases.
    m_hardNear = hardNear < m_hardNear ? hardNear : m_hardNear + (hardNear - m_hardNear) * kHardBandEase;
    m_hardFar = hardFar > m_hardFar ? hardFar : m_hardFar + (hardFar - m_hardFar) * kHardBandEase;
}

void FollowCamera::update(const FollowTarget& target, std::uint8_t rawRightX, std::uint8_t rawRightY,
                          const raycast::CollisionMesh* mesh, float seconds) {
    // The camera's view at the start of the update, which the auto-centre rule measures from.
    const anim::Vec3 viewBefore = anim::subtract(m_lookAt, m_position);
    m_lastAutoTurn = 0.0F;

    // 1. The look-at point: the target's feet plus the offset, its height eased after a sudden rise.
    followLookAt(target.feet);

    // 3. The right stick: a yaw rate and a pitch rate; any input holds the automatic rules off.
    const float yawRate = rightStickYawRate(rawRightX);
    const float pitchRate = rightStickPitchRate(rawRightY);
    if (yawRate != 0.0F || pitchRate != 0.0F) {
        m_inputHold = kInputHold;
    }

    // 4. Auto-centre: a moving target swings the camera round behind its facing, unless the stick was used lately.
    if (followTuning().autoCentre && target.turnsCamera && m_inputHold <= 0.0F) {
        autoCentre(viewBefore, target.heading, target.running, seconds);
    }

    // The sprint zoom moves the band and the target pitch (docs/research/camera.md#street); the hard band follows.
    stepSprintZoom(target.sprinting);
    stepHardBand();

    // 5. The leash: out of the band, the wanted position comes back along its line to the nearer edge.
    const anim::Vec3 offset = anim::subtract(m_wanted, m_lookAt);
    const float distance = anim::length(offset);
    if (distance > bandFar()) {
        m_wanted = anim::add(m_lookAt, withLength(offset, bandFar()));
    } else if (distance < bandNear()) {
        m_wanted = anim::add(m_lookAt, withLength(offset, bandNear()));
    }

    // 6 and 9. The pitch: stick input moves the target pitch within its limits; the wanted position turns toward it
    // at most 85°/s.
    if (pitchRate != 0.0F) {
        m_targetPitch = std::clamp(m_targetPitch + pitchRate * seconds, m_lowerPitch, m_upperPitch);
    }
    pitchToward(kPitchReturnRate * seconds);

    // The right stick's yaw turns the wanted position at once; the lag smooths it.
    if (yawRate != 0.0F) {
        yaw(yawRate * seconds);
    }

    // 11. The position lag: 22% of the wanted move each update.
    const anim::Vec3 move = anim::scale(anim::subtract(m_wanted, m_position), followTuning().positionLag);
    if (anim::length(move) >= kMinMove) {
        m_position = anim::add(m_position, move);
    }

    // 12. The hard band.
    const anim::Vec3 fromLookAt = anim::subtract(m_position, m_lookAt);
    const float actual = anim::length(fromLookAt);
    if (actual > m_hardFar) {
        m_position = anim::add(m_lookAt, withLength(fromLookAt, m_hardFar));
    } else if (actual < m_hardNear) {
        m_position = anim::add(m_lookAt, withLength(fromLookAt, m_hardNear));
    }

    // 13. The world: pull in when it hides the camera from the look-at point.
    if (mesh != nullptr) {
        collide(*mesh);
    }

    // 14. Timers.
    m_inputHold = std::max(0.0F, m_inputHold - seconds);
}

} // namespace coney::camera
