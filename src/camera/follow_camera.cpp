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

FollowPlace FollowCamera::targetPlace() const {
    return m_source ? m_source() : FollowPlace{.feet = m_targetFeet, .heading = m_targetHeading};
}

void FollowCamera::refreshTarget() {
    if (m_source) {
        const FollowPlace now = m_source();
        m_targetFeet = now.feet;
        m_targetHeading = now.heading;
    }
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
    snapAim();
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
    snapAim();
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
    snapAim();
    m_targetPitch = std::clamp(degrees * kRadians, m_lowerPitch, std::max(m_lowerPitch, upperPitch()));
    snapPitch();
    m_zoomLatched = false;
}

void FollowCamera::placeBehind(float degrees) {
    // The distance: the sprint zoom's saved edge while one is held, else the camera's own from the look-at point,
    // clamped to the band, then the preset it is nearest by the halfway rule, with the zoom step after that preset.
    const FollowSettings& s = m_settings;
    const float own = m_savedNear != 0.0F ? m_savedNear : anim::distance(m_held, m_lookAt);
    const float current = std::clamp(own, bandNear(), bandFar());
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
    // The band moves to the preset, as deep as before, and the lower pitch limit follows its far edge.
    m_bandNear = distance;
    updateLowerPitch();
    // The camera stands along the target's forward turned clockwise by `degrees` and looks back along it, so its view
    // faces the target's heading turned by 180° − `degrees` (180: behind, looking where the target looks). The target
    // is read where it stands now (Cam_Follow_PlaceAtHeading gathers it first), and place() snaps the look-at point.
    refreshTarget();
    const float turn = degrees == kBehindDegrees ? 0.0F : kPi - degrees * kRadians;
    place(m_targetFeet, distance, m_targetHeading + turn);
}

void FollowCamera::reset() {
    // The wanted near edge cleared and the target pitch back to the configured one, then behind the target at that
    // pitch with the look-at point snapped; the field of view eases back.
    m_wantedNear = -1.0F;
    m_targetPitch = std::max(m_configuredPitch, m_lowerPitch);
    placeBehind(kBehindDegrees);
    m_wantedFov = m_settings.fieldOfView;
    m_fovRate = std::abs(m_wantedFov - m_fov);
}

void FollowCamera::activate() {
    // **Coney's choice** (a stand-in): the glass tasks' update and the views' layout the original runs first, the
    // activation step's split timers and collision latches, its reset of a camera whose player index is not below the
    // player-camera count (the only branch that sets the hard band, from +0x334/+0x338, which player 1's camera with
    // one player camera never takes, so Coney keeps its hard band as it is), its lost test with two player cameras,
    // and the recovered distance it sets (+0x380) are not made here: Coney has one player camera and no recovered
    // distance (docs/research/camera.md#setting-up).
    // The activation step (0x00125e50): the look-at point snapped onto where the target stands now
    // (Cam_Follow_RefreshLookAt), so a camera made current right after a teleport looks at the new place.
    refreshTarget();
    m_lookAt = lookAtOf(m_targetFeet);
    snapAim();
    // The field of view set to the wanted one at once (slot +0x194 with +0x394), the wanted near edge cleared.
    m_fov = m_wantedFov;
    m_wantedNear = -1.0F;
    // The camera kept in its direction from the look-at point, at its distance clamped to the band.
    const anim::Vec3 offset = anim::subtract(m_position, m_lookAt);
    const float distance = std::clamp(anim::length(offset), bandNear(), bandFar());
    m_position = anim::add(m_lookAt, withLength(offset, distance));
    m_wanted = m_position;
    m_held = m_position;
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
    snapAim();
    const float across = distance * std::cos(m_targetPitch);
    const anim::Vec3 behind{std::sin(viewHeading) * across, -std::cos(viewHeading) * across,
                            distance * std::sin(m_targetPitch)};
    m_wanted = anim::add(m_lookAt, behind);
    m_position = m_wanted;
    m_held = m_wanted;
    stepHardBand();
}

void FollowCamera::placeAt(anim::Vec3 position) {
    // The look-at point snapped onto where the target stands now (Cam_Follow_RefreshLookAt), the aim point with it.
    refreshTarget();
    m_lookAt = lookAtOf(m_targetFeet);
    snapAim();
    m_wanted = position;
    m_position = position;
    m_held = position;
}

void FollowCamera::resetWhenLost() {
    // Only with one player camera. Lost: the held position of the last update farther from the look-at point across
    // the ground than 1.3 x the maximum distance (one target, which Coney's camera always has), or the camera more
    // than 30 m above or below it. The reset's pitch and field of view step runs, as for a player's target.
    if (!followTuning().onePlayerCamera) {
        return;
    }
    const float across = std::hypot(m_held.x - m_lookAt.x, m_held.y - m_lookAt.y);
    if (across > kLostDistanceShare * m_settings.maxDistance || std::abs(m_position.z - m_lookAt.z) > kLostHeight) {
        reset();
    }
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

void FollowCamera::turnPitch(float delta) {
    const float current = elevationOf(anim::subtract(m_wanted, m_lookAt));
    const float wanted = std::clamp(current + delta, m_lowerPitch, upperPitch());
    const float saved = m_targetPitch;
    m_targetPitch = wanted;
    pitchToward(kPi);
    m_targetPitch = saved;
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
        m_blockNormal = anim::Vec3{hit->normal.x, hit->normal.y, hit->normal.z};
        m_blockPoint = anim::add(m_lookAt, anim::scale(direction, hit->t));
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

void FollowCamera::snapAim() {
    m_aim = m_lookAt;
    m_leadHeading = m_targetHeading;
    m_aimLag = kAimLag;
    m_sway = 0.0F;
}

float FollowCamera::aimHeight(const FollowTarget& target, float lookAtZ) const {
    // The body point's height above 1 m, with a dead band of 0.27 m (none while down or in a tackle) and a ramp over
    // the next 0.27 m.
    float d = (target.bodyPoint ? target.bodyPoint->z : 1.0F) - 1.0F;
    const float size = std::abs(d);
    if (size <= kAimDeadBand) {
        if (!target.down) {
            return lookAtZ;
        }
        d *= std::max(0.0F, (kAimDeadBand - size) * 0.9F / kAimDeadBand + 0.1F);
    } else if (size <= 2.0F * kAimDeadBand) {
        d *= std::min(1.0F, (size - kAimDeadBand) * 0.9F / kAimDeadBand + 0.1F);
    }
    // A high look-at offset lowers a falling body's aim further.
    if (d < 0.0F && m_settings.lookAtHeight > 1.25F) {
        d -= (m_settings.lookAtHeight - 1.0F) * 0.6F;
    }
    // 35% of the way from the old height, or the goal itself where that is not between the look-at height and it.
    const float goal = lookAtZ + d;
    const float eased = m_aim.z + (goal - m_aim.z) * kAimHeightShare;
    return eased >= std::min(lookAtZ, goal) && eased <= std::max(lookAtZ, goal) ? eased : goal;
}

void FollowCamera::stepAim(const FollowTarget& target, anim::Vec3 cameraBefore, bool wasBlocked,
                           const raycast::CollisionMesh* mesh, float seconds) {
    // The view's direction as the last update left it, for the lag's across test below: the original reads its
    // camera quaternion's forward (`Quat_AxisY`, called at `0x00133434`), which faces the aim point, not the look-at
    // point. **Coney's reading**: from the camera's place before this update to the old aim point.
    const anim::Vec3 toAim = anim::subtract(m_aim, cameraBefore);
    const anim::Vec3 viewBefore = anim::length(toAim) > 1e-6F ? anim::normalise(toAim) : forward();

    // 1. The lead: a copy of the target's facing eased 35% an update; a ray along it from the look-at point, shorter
    // the more the facing runs along the view, gives the lead point.
    // Snapped while the target does not count for the cameras.
    m_leadHeading =
        target.counts ? wrapped(m_leadHeading + wrapped(target.heading - m_leadHeading) * kLeadShare) : target.heading;
    const anim::Vec3 facing{-std::sin(m_leadHeading), std::cos(m_leadHeading), 0.0F};
    // **Inferred** from the runtime (0.363 m standing behind him, where a view pitched 13° would give 0.375): the
    // view's direction across the ground.
    const anim::Vec3 toLookAt = anim::subtract(m_lookAt, cameraBefore);
    const anim::Vec3 view = withLength(anim::Vec3{toLookAt.x, toLookAt.y, 0.0F}, 1.0F);
    float lead = std::max(kLeadMin, kLeadBandShare * bandNear());
    if (m_viewBlocked) {
        const float wantedDistance = anim::length(anim::subtract(m_wanted, m_lookAt));
        if (wantedDistance > 1e-3F) {
            lead *= std::min(1.0F, anim::length(anim::subtract(m_position, m_lookAt)) / wantedDistance);
        }
    }
    if (target.grabbing) {
        lead *= kLeadGrabbing;
    }
    const float along = std::abs(facing.x * view.x + facing.y * view.y);
    float free = lead - 0.5F * lead * along;
    // **Inferred**: the world ray (`WorldManager_RayCast`) as a mesh ray with no type mask, skipping the materials
    // every camera ray skips.
    if (mesh != nullptr && free > 0.0F) {
        const raycast::Ray ray{.origin = raycast::Vec3{m_lookAt.x, m_lookAt.y, m_lookAt.z},
                               .direction = raycast::Vec3{facing.x, facing.y, facing.z},
                               .length = free};
        if (const auto hit = mesh->rayCast(ray, kSeeThroughMaterials, 0U)) {
            free = hit->t;
        }
    }
    anim::Vec3 goal = anim::add(m_lookAt, anim::scale(facing, free - 0.01F));

    // 2. The height: running or sprinting, the old aim point bobs with the hips or sways; otherwise the goal's height
    // follows the body point.
    const bool sprinting = target.gait == kGaitSprint;
    if (sprinting || target.gait == kGaitRun) {
        if (sprinting) {
            // A step of 0.125-0.25 of the limit toward the sway's side; past the limit it is held there and turns.
            m_swayRandom = m_swayRandom * 1664525U + 1013904223U;
            const float roll = 0.125F + 0.125F * static_cast<float>(m_swayRandom >> 8U) / 16777216.0F;
            m_sway += m_swayDirection * kSwayLimit * roll;
            if (std::abs(m_sway) > kSwayLimit) {
                m_sway = m_swayDirection * kSwayLimit;
                m_swayDirection = -m_swayDirection;
            }
            const anim::Vec3 side = withLength(anim::Vec3{view.y, -view.x, 0.0F}, 1.0F);
            m_aim = anim::add(m_aim, anim::scale(side, m_sway));
            m_aim.z += (2.0F * std::abs(m_sway) - kSwayLimit) * 0.6F;
        } else {
            const anim::Vec3 body = target.bodyPoint.value_or(anim::Vec3{0.0F, 0.0F, 1.0F});
            m_aim = anim::add(m_aim, anim::scale(anim::subtract(body, anim::Vec3{0.0F, 0.0F, 1.0F}), kRunBob));
            m_sway = 0.0F;
        }
    } else {
        goal.z = aimHeight(target, m_lookAt.z);
    }

    // 3. The push out of the world.
    if (mesh != nullptr) {
        const raycast::SpherePushResult pushed =
            mesh->spherePush(kAimPushRadius, raycast::Vec3{goal.x, goal.y, goal.z}, kSeeThroughMaterials);
        if (pushed.touched) {
            goal = anim::Vec3{pushed.centre.x, pushed.centre.y, pushed.centre.z};
        }
    }

    // 4. The lag, from 0.06: longer the more the target moves across the view and the faster he goes, its speed share
    // coming back after a blocked view clears; 2.5 × when he is fast and the aim point trails, or a script walks him
    // fast; eased 3% an update. The across test and the 2.5 × gate read the 3D velocity (a fall counts), the speed
    // share the horizontal speed (docs/research/camera.md#aim-point, #long-fall-coney).
    if (wasBlocked && !m_viewBlocked) {
        m_unblockTimer = kUnblockSeconds;
    }
    const float speed = std::hypot(target.velocity.x, target.velocity.y);
    const float speed3d = anim::length(target.velocity);
    float across = 1.0F;
    if (speed3d > target.walkSpeed && speed3d > 1e-4F) {
        across = std::abs(anim::dot(target.velocity, viewBefore) / speed3d);
    }
    float lag = kAimLag * (1.0F + (1.0F - across) * 0.5F);
    if (target.sprintSpeed > 0.0F) {
        float share = std::max(0.0F, (speed - target.walkSpeed) / target.sprintSpeed);
        // The timer runs down only while the target is faster than his walk.
        if (m_unblockTimer > 0.0F && share > 0.0F) {
            share *= (kUnblockSeconds - m_unblockTimer) * (4.0F / 3.0F);
            m_unblockTimer = std::max(0.0F, m_unblockTimer - seconds);
        }
        lag += share * lag * ((1.0F - across) * 0.5625F + 0.1875F);
    }
    // The overshoot test: × 2.5 when this update's aim point, moved by the lag built so far, would still end farther
    // from the goal than the look-at point is (squared 3D distances); only when it would not, a target the stick does
    // not move (`0x00133dcc`-`0x00133ec8`, docs/research/camera.md#aim-point). **Coney's choice** (a stand-in): the
    // slope latch that also passes the first test (`+0x460`) is not modelled, as Coney has no slope latches yet.
    if (speed3d > target.walkSpeed + 0.18F * (target.runSpeed - target.walkSpeed)) {
        const anim::Vec3 next = anim::add(m_aim, anim::scale(anim::subtract(goal, m_aim), lag));
        const anim::Vec3 toLookAt3d = anim::subtract(goal, m_lookAt);
        const anim::Vec3 toNext = anim::subtract(goal, next);
        const float leadSquared = anim::dot(toLookAt3d, toLookAt3d);
        const bool trails = leadSquared > 1e-5F && leadSquared < anim::dot(toNext, toNext);
        if (trails || !target.stickOwned) {
            lag *= 2.5F;
        }
    }
    m_aimLag += (lag - m_aimLag) * kAimLagEase;
    // **Coney's reading**: the long fall's 0.6 and the halving for a target that does not count apply to this
    // update's move, not to the eased value.
    float share = target.longFall ? kLongFallLag : m_aimLag;
    if (!target.counts) {
        share *= 0.5F;
    }
    // A wall that holds the camera (steeper than 53.13°) moves it by a share from how far the feet stand in front of
    // the wall: 30% within 1 m, nothing from 3.5 m.
    if (m_viewBlocked && std::abs(m_blockNormal.z) < 0.6F) {
        const float inFront = anim::dot(m_blockNormal, anim::subtract(target.feet, m_blockPoint));
        share = std::min(1.0F, (3.5F - inFront) * 0.4F) * 0.3F;
    }

    // 5. The move: the share of the way to the goal, never farther from the look-at point in plan than it.
    m_aim = anim::add(m_aim, anim::scale(anim::subtract(goal, m_aim), share));
    const float goalReach = std::hypot(goal.x - m_lookAt.x, goal.y - m_lookAt.y);
    const float reach = std::hypot(m_aim.x - m_lookAt.x, m_aim.y - m_lookAt.y);
    if (reach > goalReach && reach > 1e-6F) {
        const float pull = goalReach / reach;
        m_aim.x = m_lookAt.x + (m_aim.x - m_lookAt.x) * pull;
        m_aim.y = m_lookAt.y + (m_aim.y - m_lookAt.y) * pull;
    }
}

void FollowCamera::update(const FollowTarget& target, std::uint8_t rawRightX, std::uint8_t rawRightY,
                          const raycast::CollisionMesh* mesh, float seconds) {
    // **Coney stand-ins** for the parts of Cam_Follow_Update not built (docs/research/camera.md#update): the look-at
    // offset's 15% ease, the tagging, fight-stance, hidden-in-shadow and aiming states and their look-at heights, the
    // look-behind, the queued heading (+0x43c), the placed heading (+0x3f0), the heading target and pitch override
    // (+0x350, +0x354), the height probe, the head look, aiming's and a grapple's lags, the tilt over obstacles, the
    // roll's ease, the split screen, the queued hand-over and the fade timer. The shake is the manager's (Cameras).
    // The camera's view at the start of the update, which the auto-follow rules measure from.
    const anim::Vec3 viewBefore = anim::subtract(m_lookAt, m_position);
    const anim::Vec3 cameraBefore = m_position;
    const bool wasBlocked = m_viewBlocked;
    m_lastAutoTurn = 0.0F;
    m_lastFrameTurn = 0.0F;
    observe(target);

    // The band's ease toward a wanted near edge (the sprint zoom's), early in the update.
    easeBand(seconds);

    // 1. The look-at point: the target's feet plus the offset, its move limited by its length.
    followLookAt(target);

    // 2. The field of view.
    easeFieldOfView(seconds);

    // The reset when lost (the original's step 7): far behind the look-at point after a long move, the camera is put
    // behind the target at once.
    resetWhenLost();

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

    // 6. The pitch toward its target: the wanted position's pitch about the look-at point is measured and turned all
    // the way to the target pitch, so a leash pull along a steep line (a long fall) cannot carry it up. Skipped in
    // the height hold (the original's step 11, the call at 0x0012beac with the pitch 0x0012d908 measures).
    if (!m_heightHold) {
        pitchToward(kPi);
    }

    // 7. The height hold: the wanted position's height eases back toward the height above the look-at point it held.
    if (m_heightHold) {
        const float goal = m_lookAt.z + m_heldHeight;
        m_wanted.z += (goal - m_wanted.z) * kHeightHoldShare * m_heightHoldScale;
    }

    // 9. Stick input moves the target pitch within its limits and turns the wanted position by the same rate;
    // without it, the wanted position goes back to the target at most 85°/s (outside the height hold). The
    // original's step 16 (0x0012c60c-0x0012c6a8).
    if (pitchRate != 0.0F) {
        m_targetPitch = std::clamp(m_targetPitch + pitchRate * seconds, m_lowerPitch, upperPitch());
        turnPitch(pitchRate * seconds);
    } else if (!m_heightHold) {
        pitchToward(kPitchReturnRate * seconds);
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
    m_held = m_position;

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

    // The aim point the view faces, from this update's look-at point and the target's body.
    stepAim(target, cameraBefore, wasBlocked, mesh, seconds);

    // 14. Timers and the camera's clock.
    m_inputHold = std::max(0.0F, m_inputHold - seconds);
    m_timer = std::max(0.0F, m_timer - seconds);
    m_clock += seconds;
}

} // namespace coney::camera
