// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/follow_camera.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "camera/follow_collision.h"

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
// The default rule's: a rise to 60°/s by 45°, 60°/s to 135°, then from 120°/s down to 60°/s at 157.5°.
constexpr float kDefaultSlope = 2.667F;
constexpr float kDefaultSteady = 60.0F;
constexpr float kDefaultFallFrom = 135.0F;
constexpr float kDefaultFallTop = 120.0F;
// The upper pitch limits of the zoom distances, degrees.
constexpr float kUpperAtMinimum = 50.0F;
constexpr float kUpperAboveDefault = 40.0F;
constexpr float kUpperAtDefault = 30.0F;
// Two zoom distances closer than this are the same step.
constexpr float kSameZoom = 1e-3F;

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

float defaultFollowRate(float angle) {
    const float a = std::abs(angle) / kRadians;
    float rate = 0.0F; // degrees a second
    if (a < kCentreFrom || a > kCentreBehind) {
        rate = 0.0F;
    } else if (a < kCentreMid) {
        rate = (a - kCentreFrom) * kDefaultSlope;
    } else if (a <= kDefaultFallFrom) {
        rate = kDefaultSteady;
    } else {
        rate = kDefaultFallTop -
               (a - kDefaultFallFrom) * (kDefaultFallTop - kDefaultSteady) / (kCentreBehind - kDefaultFallFrom);
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
      m_zoomDistance(settings.maxDistance), m_bandNear(settings.leashNear),
      m_bandWidth(settings.leashFar - settings.leashNear) {
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

float FollowCamera::upperPitch() const {
    if (std::abs(m_zoomDistance - m_settings.minDistance) < kSameZoom) {
        return kUpperAtMinimum * kRadians;
    }
    if (m_zoomDistance > m_settings.defaultDistance + kSameZoom) {
        return kUpperAboveDefault * kRadians;
    }
    return (followTuning().cameraOption ? kUpperAtDefault : kUpperAtMinimum) * kRadians;
}

void FollowCamera::enableSprintZoom(bool on) {
    m_zoomOn = on;
    if (!on) {
        m_wantedNear = -1.0F;
        m_savedPitch.reset();
        m_zoomActive = false;
        m_zoomLatched = false;
        m_zoomArmed = false;
    }
}

void FollowCamera::holdHeight(bool on, float scale) {
    m_heightHold = on;
    m_heightHoldScale = scale;
    m_heldHeight = m_wanted.z - m_lookAt.z;
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
    const float next = std::clamp(m_targetPitch, current - maxStep, current + maxStep);
    // Keep the heading across the ground; straight behind +y when the camera is right above the point.
    const float headingX = across > 1e-6F ? offset.x / across : 0.0F;
    const float headingY = across > 1e-6F ? offset.y / across : -1.0F;
    const float newAcross = distance * std::cos(next);
    m_wanted = anim::add(m_lookAt, anim::Vec3{headingX * newAcross, headingY * newAcross, distance * std::sin(next)});
}

void FollowCamera::stepZoom(float distance) { m_zoomDistance = distance; }

void FollowCamera::collide(const raycast::CollisionMesh& mesh, anim::Vec3 targetFeet) {
    const anim::Vec3 view = anim::subtract(m_position, m_lookAt);
    const float distance = anim::length(view);
    m_viewBlocked = false;
    if (distance < 1e-6F) {
        return;
    }
    const anim::Vec3 direction = anim::scale(view, 1.0F / distance);
    // The side probes: when one side has clearly more room, swing toward it, a share of the way to even them up.
    const float angle = probeAngle(distance, bandNear(), bandFar());
    const std::array<float, 2> room = sideRoom(mesh, m_lookAt, direction, distance, angle, targetFeet);
    if (const float difference = room[0] - room[1]; std::abs(difference) > kSwingDifference) {
        yaw(kSwingShare * difference / 2.0F);
    }
    // The main ray: a hit stops the next update's auto-follow and pulls the camera in short of it.
    if (const auto hit = castViewRay(mesh, m_lookAt, direction, distance, targetFeet); hit) {
        m_viewBlocked = true;
        const float pulled = std::max(followTuning().minCollisionDistance, hit->t - followTuning().collisionMargin);
        m_position = anim::add(m_lookAt, anim::scale(direction, std::min(pulled, distance)));
    }
}

void FollowCamera::followLookAt(const FollowTarget& target) {
    const anim::Vec3 wanted = anim::add(target.feet, anim::Vec3{0.0F, 0.0F, m_settings.lookAtHeight});
    // In the air the point follows the feet directly; otherwise a long move is covered a share at a time.
    if (target.airborne) {
        m_lookAt = wanted;
        return;
    }
    const anim::Vec3 move = anim::subtract(wanted, m_lookAt);
    const float d = anim::length(move);
    float share = 1.0F;
    if (d > kLookAtSlow) {
        share = kLookAtSlowShare;
    } else if (d > kLookAtFree) {
        share = 1.0F - 2.0F * (d - kLookAtFree);
    }
    m_lookAt = anim::add(m_lookAt, anim::scale(move, share));
}

void FollowCamera::autoFollow(anim::Vec3 view, const FollowTarget& target, float seconds) {
    // The gates: no camera input lately, the view not blocked on the last update, the stick not pulled back.
    if (m_inputHold > 0.0F || m_viewBlocked || target.stickBack || std::hypot(view.x, view.y) < 1e-6F) {
        return;
    }
    // The auto-centre rule follows a walk, run or sprint; the default rule only a run or sprint.
    const bool running = target.gait == kGaitRun || target.gait == kGaitSprint;
    const bool centre = followTuning().autoCentre;
    if (!running && !(centre && target.gait == kGaitWalk)) {
        return;
    }
    // The angle between the camera's view across the ground and the target's facing; each update turns by the rule's
    // rate (which may be negative: a small turn away) at most the whole angle.
    const float difference = wrapped(target.heading - headingOfView(view));
    const float angle = std::abs(difference);
    const float rate = centre ? autoCentreRate(angle, running) : defaultFollowRate(angle);
    const float step = std::min(angle, rate * seconds);
    if (step == 0.0F) {
        return;
    }
    m_lastAutoTurn = difference >= 0.0F ? step : -step;
    yaw(m_lastAutoTurn);
}

void FollowCamera::latchSprint(bool sprinting, float seconds) {
    // The first update of a sprint arms the zoom (when it is switched on); the time at the sprint gait counts from
    // there (**Coney's reading** of `+0x36c`: zeroed off the sprint gait, so every sprint arms afresh).
    if (sprinting && m_sprintTime == 0.0F) {
        m_zoomArmed = m_zoomOn;
    }
    m_sprintTime = sprinting ? m_sprintTime + seconds : 0.0F;
    // Armed and sprinting: latch, and run the zoom from now on. The first update off the sprint gait unlatches, and
    // the zoom goes back once 250 ms have passed.
    if (sprinting && m_zoomArmed) {
        m_zoomArmed = false;
        m_zoomLatched = true;
    }
    if (m_zoomLatched && sprinting && !m_zoomActive) {
        m_zoomActive = true;
        m_zoomFrom = m_clock - 1.0;
    } else if (m_zoomLatched && !sprinting) {
        m_zoomLatched = false;
        m_zoomActive = true;
        m_zoomFrom = m_clock + kZoomBackDelay;
    }
}

void FollowCamera::zoomPitchToward(float goal, float seconds) {
    // Nothing on the update the timer is set; then a straight line that arrives as the timer runs out.
    if (m_timer >= kZoomSeconds) {
        return;
    }
    const float left = goal - m_targetPitch;
    const float step = m_timer > 0.0F ? std::abs(left) / m_timer * seconds : kZoomPitchRate * seconds;
    m_targetPitch = std::abs(left) <= step ? goal : m_targetPitch + std::copysign(step, left);
}

void FollowCamera::sprintZoom(bool sprinting, float seconds) {
    const FollowTuning& tuning = followTuning();
    if (sprinting) {
        // The first time: save the band and the zoom, start the timer, and pull the band in to the minimum distance
        // (with the camera option; without it the band stays and the way back goes to the maximum less 0.5). A way
        // back still under way is saved by where it was going (**Coney's choice**, so a quick second sprint does not
        // keep a band left half-way).
        if (m_savedNear == 0.0F) {
            m_savedNear = m_wantedNear > 0.0F ? m_wantedNear : m_bandNear;
            m_savedZoom = m_zoomDistance;
            m_timer = kZoomSeconds;
            if (tuning.cameraOption) {
                m_wantedNear = m_settings.minDistance;
                stepZoom(m_settings.defaultDistance);
            } else {
                m_savedNear = m_settings.maxDistance - m_bandWidth;
                m_savedZoom = m_settings.defaultDistance;
            }
        }
        if (!m_savedPitch) {
            m_savedPitch = m_targetPitch;
        }
        zoomPitchToward(m_settings.sprintPitchDegrees * kRadians, seconds);
        return;
    }
    // Not sprinting: the band and the zoom back to what was saved, over the timer, and the pitch with them.
    if (m_savedNear != 0.0F) {
        m_wantedNear = m_savedNear;
        m_savedNear = 0.0F;
        stepZoom(m_savedZoom);
        m_timer = kZoomSeconds;
    }
    if (m_savedPitch) {
        zoomPitchToward(*m_savedPitch, seconds);
    }
    // Both arrived: the zoom is over.
    if (m_wantedNear <= 0.0F && (!m_savedPitch || std::abs(m_targetPitch - *m_savedPitch) < kArrived)) {
        m_zoomActive = false;
        m_savedPitch.reset();
    }
}

void FollowCamera::easeBand(float seconds) {
    if (m_wantedNear <= 0.0F) {
        return;
    }
    // d × |d| / T × dt over the timer (so it slows as it nears), or a fixed share a second without one; never past
    // the goal.
    const float d = m_wantedNear - m_bandNear;
    const float step = m_timer > 0.0F ? d * std::abs(d) / m_timer * seconds
                                      : d * (m_zoomActive ? kBandEaseZoom : kBandEaseFree) * seconds;
    m_bandNear = std::abs(step) >= std::abs(d) ? m_wantedNear : m_bandNear + step;
    if (std::abs(m_wantedNear - m_bandNear) < kArrived) {
        m_bandNear = m_wantedNear;
        m_wantedNear = -1.0F;
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
    // The camera's view at the start of the update, which the auto-follow rules measure from.
    const anim::Vec3 viewBefore = anim::subtract(m_lookAt, m_position);
    m_lastAutoTurn = 0.0F;

    // The band's ease toward a wanted near edge (the sprint zoom's), early in the update.
    easeBand(seconds);

    // 1. The look-at point: the target's feet plus the offset, its move limited by its length.
    followLookAt(target);

    // 3. The right stick: a yaw rate and a pitch rate; any input holds the automatic rules off.
    const float yawRate = rightStickYawRate(rawRightX);
    const float pitchRate = rightStickPitchRate(rawRightY);
    if (yawRate != 0.0F || pitchRate != 0.0F) {
        m_inputHold = kInputHold;
    }

    // 4. Auto-follow: a moving target swings the camera round behind its facing.
    autoFollow(viewBefore, target, seconds);

    // The sprint zoom: latched by the sprint gait; its function runs once the game time has passed its start.
    const bool sprinting = target.gait == kGaitSprint;
    latchSprint(sprinting, seconds);
    if (m_zoomActive && m_clock > m_zoomFrom) {
        sprintZoom(sprinting, seconds);
    }
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
        m_targetPitch = std::clamp(m_targetPitch + pitchRate * seconds, m_lowerPitch, upperPitch());
    }
    pitchToward(kPitchReturnRate * seconds);

    // 7. The height hold: the wanted position's height eases back toward the height above the look-at point it held.
    if (m_heightHold) {
        const float goal = m_lookAt.z + m_heldHeight;
        m_wanted.z += (goal - m_wanted.z) * kHeightHoldShare * m_heightHoldScale;
    }

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

    // 13. The world: swing away from it, pull in when it hides the camera from the look-at point.
    m_viewBlocked = false;
    if (mesh != nullptr) {
        collide(*mesh, target.feet);
    }

    // 14. Timers and the camera's clock.
    m_inputHold = std::max(0.0F, m_inputHold - seconds);
    m_timer = std::max(0.0F, m_timer - seconds);
    m_clock += seconds;
}

} // namespace coney::camera
