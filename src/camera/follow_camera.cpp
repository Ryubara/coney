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
// A held lock-on button lets the hard band's far edge out to this share of the maximum distance.
constexpr float kLockHeldFarShare = 1.1F;
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
      m_bandWidth(settings.leashFar - settings.leashNear), m_configuredPitch(settings.pitchDegrees * kRadians),
      m_fov(settings.fieldOfView), m_wantedFov(settings.fieldOfView), m_targetHeading(targetHeading) {
    // Behind the target's facing, at the band's near edge, at the target pitch.
    place(targetFeet, settings.leashNear, targetHeading);
}

anim::Vec3 FollowCamera::lookAtOf(anim::Vec3 feet) const {
    return anim::add(feet, anim::Vec3{m_settings.lookAtX, m_settings.lookAtY, m_settings.lookAtHeight});
}

void FollowCamera::updateLowerPitch() {
    m_lowerPitch = std::max(kLowestPitchDegrees * kRadians,
                            std::atan((1.0F - m_settings.lookAtHeight) / std::max(bandFar(), 1e-3F)));
    m_targetPitch = std::max(m_targetPitch, m_lowerPitch);
}

float FollowCamera::zoomStepFor(float near) const {
    const FollowSettings& s = m_settings;
    if (near <= s.minDistance + kZoomStepNearShare * (s.defaultDistance - s.minDistance)) {
        return s.defaultDistance;
    }
    if (near <= s.defaultDistance + kZoomStepFarShare * (s.maxDistance - s.defaultDistance)) {
        return s.maxDistance;
    }
    return s.minDistance;
}

void FollowCamera::snapPitch() {
    pitchToward(kPi);
    m_position = m_wanted;
}

void FollowCamera::observe(const FollowTarget& target) {
    m_targetFeet = target.feet;
    m_targetHeading = target.heading;
}

void FollowCamera::configure(const FollowSettings& settings) {
    // The three distances, each clamped against the others; the band at the default, min(0.5, max - min) deep.
    m_settings = settings;
    m_settings.minDistance = std::max(settings.minDistance, 0.0F);
    m_settings.maxDistance = std::max(settings.maxDistance, m_settings.minDistance);
    m_settings.defaultDistance = std::clamp(settings.defaultDistance, m_settings.minDistance, m_settings.maxDistance);
    m_bandWidth = std::min(kBandDepth, m_settings.maxDistance - m_settings.minDistance);
    m_bandNear = m_settings.defaultDistance;
    m_settings.leashNear = m_bandNear;
    m_settings.leashFar = m_bandNear + m_bandWidth;
    // The maximum's setter recomputes the lower pitch limit from it.
    m_lowerPitch = std::max(kLowestPitchDegrees * kRadians,
                            std::atan((1.0F - m_settings.lookAtHeight) / std::max(m_settings.maxDistance, 1e-3F)));
    // The pitch, configured and wanted, reached at once; the field of view eases to the new one over 1 s.
    m_configuredPitch = m_settings.pitchDegrees * kRadians;
    m_targetPitch = std::max(m_configuredPitch, m_lowerPitch);
    m_lookAt = lookAtOf(m_targetFeet);
    snapPitch();
    m_wantedFov = m_settings.fieldOfView;
    m_fovRate = std::abs(m_wantedFov - m_fov);
    m_heightHoldScale = kConfiguredHoldScale;
    // Last, the band to the minimum with one player camera (CamSetFollowZoom(0)); with two, to the default.
    setZoom(FollowZoom::Close);
}

void FollowCamera::setZoom(FollowZoom preset) {
    FollowZoom zoom = preset;
    if (zoom == FollowZoom::Close && !followTuning().onePlayerCamera) {
        zoom = FollowZoom::Default;
    }
    // The look-at point snapped, with no ease.
    m_lookAt = lookAtOf(m_targetFeet);
    // The band's near edge at the preset, kept within the distances; the zoom step the next preset.
    const FollowSettings& s = m_settings;
    float near = s.minDistance;
    float step = s.defaultDistance;
    switch (zoom) {
    case FollowZoom::Close:
        break;
    case FollowZoom::Default:
        near = s.defaultDistance;
        step = s.maxDistance;
        break;
    case FollowZoom::Far:
        near = s.maxDistance - m_bandWidth;
        step = s.minDistance;
        break;
    }
    m_bandNear = std::clamp(near, s.minDistance, std::max(s.minDistance, s.maxDistance - m_bandWidth));
    updateLowerPitch();
    stepZoom(step);
}

void FollowCamera::setPitch(float degrees) {
    m_lookAt = lookAtOf(m_targetFeet);
    m_targetPitch = std::clamp(degrees * kRadians, m_lowerPitch, std::max(m_lowerPitch, upperPitch()));
    snapPitch();
    m_zoomLatched = false;
}

void FollowCamera::reset() {
    // The distance: the current one clamped to the band, then the preset it is nearest by the halfway rule.
    const FollowSettings& s = m_settings;
    const float current = std::clamp(anim::distance(m_position, m_lookAt), bandNear(), bandFar());
    float distance = s.maxDistance - kBandDepth;
    float step = s.minDistance;
    if (current <= s.minDistance + (s.defaultDistance - s.minDistance) / 2.0F) {
        distance = s.minDistance;
        step = s.defaultDistance;
    } else if (current <= s.defaultDistance + (s.maxDistance - s.defaultDistance) / 2.0F) {
        distance = s.defaultDistance;
        step = s.maxDistance;
    }
    stepZoom(step);
    // Behind the target at that distance at the configured pitch, the look-at point snapped; the field of view back.
    m_wantedNear = -1.0F;
    m_targetPitch = std::max(m_configuredPitch, m_lowerPitch);
    place(m_targetFeet, distance, m_targetHeading);
    m_wantedFov = s.fieldOfView;
    m_fovRate = std::abs(m_wantedFov - m_fov);
}

void FollowCamera::activate() {
    // The look-at point snapped; the camera kept in its direction from it, at its distance clamped to the band.
    m_lookAt = lookAtOf(m_targetFeet);
    const anim::Vec3 offset = anim::subtract(m_position, m_lookAt);
    const float distance = std::clamp(anim::length(offset), bandNear(), bandFar());
    m_position = anim::add(m_lookAt, withLength(offset, distance));
    m_wanted = m_position;
    // The hard band set to the band, the wanted near edge cleared.
    m_hardNear = bandNear();
    m_hardFar = bandFar();
    m_wantedNear = -1.0F;
}

void FollowCamera::easeFieldOfView(float seconds) {
    if (m_fov == m_wantedFov) {
        return;
    }
    // Over the timed move's time left when one runs, else at the configured rate, at most 7.5° a second.
    const float left = m_wantedFov - m_fov;
    const float step = m_timer > 0.0F ? std::abs(left) / m_timer * seconds : std::min(m_fovRate, kFovRateCap) * seconds;
    m_fov = std::abs(left) <= step ? m_wantedFov : m_fov + std::copysign(step, left);
}

bool FollowCamera::combat(const FollowTarget& target, float seconds) {
    const bool on = target.lockOn && target.enemy.has_value();
    if (on && !m_combatOn) {
        // Entry: save the band's wanted near edge (the sprint zoom's saved edge, else one in progress, else the near
        // edge), ease the band to 2.4 m and set the target pitch to 15°.
        m_combatOn = true;
        if (m_savedNear != 0.0F) {
            m_combatSaved = m_savedNear;
        } else {
            m_combatSaved = m_wantedNear > 0.0F ? m_wantedNear : m_bandNear;
        }
        m_wantedNear = kCombatNear;
        m_targetPitch = kCombatPitchDegrees * kRadians;
    } else if (!on && m_combatOn) {
        // Exit: the saved edge back; the target pitch stays.
        m_combatOn = false;
        m_wantedNear = m_combatSaved;
        m_combatSaved = 0.0F;
    }
    if (!m_combatOn) {
        return false;
    }
    // The enemy's angle at the look-at point from the camera's view across the ground; outside 25-29°, a share of
    // the way to 27°, capped beyond 29°.
    const anim::Vec3 view = anim::subtract(m_lookAt, m_position);
    const anim::Vec3 toEnemy = anim::subtract(*target.enemy, m_lookAt);
    if (std::hypot(view.x, view.y) < 1e-6F || std::hypot(toEnemy.x, toEnemy.y) < 1e-6F) {
        return true;
    }
    const float angle = wrapped(headingOfView(toEnemy) - headingOfView(view));
    const float off = std::abs(angle);
    if (off >= kCombatFrameLow * kRadians && off <= kCombatFrameHigh * kRadians) {
        return true;
    }
    float turn = (off - kCombatFrameDegrees * kRadians) * kCombatFrameShare;
    if (off > kCombatFrameHigh * kRadians) {
        turn = std::min(turn, kCombatFrameRate * seconds);
    }
    m_lastFrameTurn = angle >= 0.0F ? turn : -turn;
    yaw(m_lastFrameTurn);
    return true;
}

void FollowCamera::keepInView(anim::Vec3 point, float range, float seconds) {
    // With a range, only a target within it (**Coney's choice**: the ray the original also casts is left out).
    if (range > 0.0F && anim::distance(point, m_lookAt) > range) {
        return;
    }
    const anim::Vec3 view = anim::subtract(m_lookAt, m_position);
    const anim::Vec3 toPoint = anim::subtract(point, m_position);
    if (std::hypot(view.x, view.y) < 1e-6F || std::hypot(toPoint.x, toPoint.y) < 1e-6F) {
        return;
    }
    // More than a quarter of the field of view off the view's direction: 35% of the excess, at most 270°/s.
    const float angle = wrapped(headingOfView(toPoint) - headingOfView(view));
    const float excess = std::abs(angle) - m_fov * kRadians * kKeepInViewFactor;
    if (excess <= 0.0F) {
        return;
    }
    const float turn = std::min(excess * kKeepInViewShare, kKeepInViewRate * seconds);
    m_lastFrameTurn = angle >= 0.0F ? turn : -turn;
    yaw(m_lastFrameTurn);
}

void FollowCamera::place(anim::Vec3 targetFeet, float distance, float viewHeading) {
    // The look-at point above the feet, and the camera behind it along the view's heading at the target pitch.
    m_targetFeet = targetFeet;
    m_lookAt = lookAtOf(targetFeet);
    const float across = distance * std::cos(m_targetPitch);
    const anim::Vec3 behind{std::sin(viewHeading) * across, -std::cos(viewHeading) * across,
                            distance * std::sin(m_targetPitch)};
    m_wanted = anim::add(m_lookAt, behind);
    m_position = m_wanted;
    stepHardBand();
}

void FollowCamera::placeAt(anim::Vec3 position) {
    m_lookAt = lookAtOf(m_targetFeet);
    m_wanted = position;
    m_position = position;
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
    return (followTuning().onePlayerCamera ? kUpperAtDefault : kUpperAtMinimum) * kRadians;
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
    const anim::Vec3 wanted = lookAtOf(target.feet);
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
    // The gates: no camera input lately, no blocked view since the player last stopped (the latch, which the last
    // update's blocked ray sets), the stick not pulled back.
    if (m_inputHold > 0.0F || m_viewBlocked || m_viewLatch || target.stickBack || std::hypot(view.x, view.y) < 1e-6F) {
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

void FollowCamera::latchSprint(bool sprinting, const FollowTarget& target, float seconds) {
    // The first update of a sprint arms the zoom (when it is switched on); the time at the sprint gait counts from
    // there (**Coney's reading** of `+0x36c`: zeroed off the sprint gait, so every sprint arms afresh).
    if (sprinting && m_sprintTime == 0.0F) {
        m_zoomArmed = m_zoomOn;
    }
    m_sprintTime = sprinting ? m_sprintTime + seconds : 0.0F;
    // Armed and sprinting, with no enemies or the nearest within 12 m (the squared distance under 144, 0x0012b504):
    // latch, and run the zoom from now on. The first update off the sprint gait unlatches, and the zoom goes back once
    // 250 ms have passed. Out of the enemies' range the arm waits, and latches when one comes near.
    const bool enemiesAllow = !target.nearestEnemy || *target.nearestEnemy < kZoomEnemyRange;
    if (sprinting && m_zoomArmed && enemiesAllow) {
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
        // (with one player camera; with two the band stays and the way back goes to the maximum less 0.5). A way
        // back still under way is saved by where it was going (**Coney's choice**, so a quick second sprint does not
        // keep a band left half-way).
        if (m_savedNear == 0.0F) {
            m_savedNear = m_wantedNear > 0.0F ? m_wantedNear : m_bandNear;
            m_savedZoom = m_zoomDistance;
            m_timer = kZoomSeconds;
            if (tuning.onePlayerCamera) {
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
    // The zoom step follows the near edge as CamSetFollowZoom sets it.
    stepZoom(zoomStepFor(m_bandNear));
}

void FollowCamera::stepHardBand(bool lockHeld) {
    // The leash band widened by max(5%, 0.2 m) and max(6%, 0.35 m); a held lock-on button doubles the far edge, at
    // most 1.1 × the maximum distance.
    const float near = bandNear();
    const float far = bandFar();
    const float hardNear = near - std::max(kHardNearShare * near, kHardNearMinimum);
    float hardFar = far + std::max(kHardFarShare * far, kHardFarMinimum);
    if (lockHeld) {
        hardFar = std::max(hardFar, std::min(2.0F * hardFar, kLockHeldFarShare * m_settings.maxDistance));
    }
    // Widening is at once (and a new camera, whose edges start at the extremes, starts there); shrinking eases.
    m_hardNear = hardNear < m_hardNear ? hardNear : m_hardNear + (hardNear - m_hardNear) * kHardBandEase;
    m_hardFar = hardFar > m_hardFar ? hardFar : m_hardFar + (hardFar - m_hardFar) * kHardBandEase;
}

void FollowCamera::update(const FollowTarget& target, std::uint8_t rawRightX, std::uint8_t rawRightY,
                          const raycast::CollisionMesh* mesh, float seconds) {
    // The camera's view at the start of the update, which the auto-follow rules measure from.
    const anim::Vec3 viewBefore = anim::subtract(m_lookAt, m_position);
    m_lastAutoTurn = 0.0F;
    m_lastFrameTurn = 0.0F;
    observe(target);

    // The band's ease toward a wanted near edge (the sprint zoom's), early in the update.
    easeBand(seconds);

    // 1. The look-at point: the target's feet plus the offset, its move limited by its length.
    followLookAt(target);

    // 2. The field of view.
    easeFieldOfView(seconds);

    // 3. The right stick (unless CamEnable(0) turned it off): a yaw rate and a pitch rate; any input holds the
    // automatic rules off.
    const float yawRate = m_stickOn && m_padStickOn ? rightStickYawRate(rawRightX) : 0.0F;
    const float pitchRate = m_stickOn && m_padStickOn ? rightStickPitchRate(rawRightY) : 0.0F;
    if (yawRate != 0.0F || pitchRate != 0.0F) {
        m_inputHold = kInputHold;
    }

    // The combat camera frames the enemy; that is this update's turn. Otherwise a watched human is kept in view in
    // place of auto-follow (with no stick turn this update), or 4. auto-follow swings the camera round behind a moving
    // target's facing.
    if (!combat(target, seconds)) {
        if (target.secondary) {
            if (yawRate == 0.0F) {
                keepInView(*target.secondary, target.secondaryRange, seconds);
            }
        } else {
            autoFollow(viewBefore, target, seconds);
        }
    }

    // The sprint zoom: latched by the sprint gait; its function runs once the game time has passed its start.
    const bool sprinting = target.gait == kGaitSprint;
    latchSprint(sprinting, target, seconds);
    if (m_zoomActive && m_clock > m_zoomFrom) {
        sprintZoom(sprinting, seconds);
    }
    stepHardBand(target.lockHeld);

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

    // 13. The world: swing away from it, pull in when it hides the camera from the look-at point. A blocked view
    // latches until the target has stood a moment.
    m_viewBlocked = false;
    if (mesh != nullptr) {
        collide(*mesh, target.feet);
    }
    m_standingUpdates = target.gait == 0 && !target.airborne ? m_standingUpdates + 1 : 0;
    if (m_viewBlocked) {
        m_viewLatch = true;
    } else if (m_standingUpdates >= kLatchClearUpdates) {
        m_viewLatch = false;
    }

    // 14. Timers and the camera's clock.
    m_inputHold = std::max(0.0F, m_inputHold - seconds);
    m_timer = std::max(0.0F, m_timer - seconds);
    m_clock += seconds;
}

} // namespace coney::camera
