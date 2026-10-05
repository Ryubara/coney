// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "animation/anim_math.h"
#include "raycast/collision_mesh.h"

// The follow camera behind the player in normal play (Cam_Follow): a leash camera that keeps its look-at point on the
// player, is dragged back into a distance band when the player moves away, swings round behind a moving player (the
// auto-follow rules), zooms in while the player sprints, covers a share of its wanted move each update, holds a pitch,
// turns with the right stick and pulls in or swings away when the world is in the way. Game axes (z up), stepped with
// the characters at 30 Hz; its clock is the sum of its updates, so a scripted pad gives the same views on every run
// (test mode). Research: docs/research/camera.md, docs/research/feel.md

namespace coney::camera {

/// What `CfgFollowCamera` sets, as read in level99's street (docs/research/camera.md#level99-values,
/// docs/research/camera.md#street).
struct FollowSettings {
    float minDistance = 3.0F;
    float maxDistance = 6.6F;
    float defaultDistance = 4.8F;
    float pitchDegrees = 13.0F;
    float lookAtHeight = 1.4F; ///< The look-at offset above the target's feet.
    /// The leash band (`+0x32c` / `+0x330`): the default distance, and the default + min(0.5, max − min).
    float leashNear = 4.8F;
    float leashFar = 5.3F;
    float sprintPitchDegrees = 7.0F; ///< The target pitch the sprint zoom moves to.
};

/// The follow camera's values the debug menus may edit while the game runs (Coney's tunables,
/// docs/guides/debug-menu.md#tunables), defaulting to FollowCamera's constants; read every update through
/// followTuning().
struct FollowTuning {
    float positionLag = 0.22F;         ///< FollowCamera::kPositionLag.
    float collisionMargin = 0.2F;      ///< FollowCamera::kCollisionMargin.
    float minCollisionDistance = 0.5F; ///< FollowCamera::kMinCollisionDistance.
    /// The per-pad auto-follow option bytes (`0x0050b240` / `0x0050b248`), 1 by default: the auto-centre rule; off,
    /// the default rule.
    bool autoCentre = true;
    /// One player camera (`0x0050b19c` = 1, the count `0x00122ed0` makes): the sprint zoom pulls the band in to the
    /// minimum distance and the default zoom's upper pitch limit is 30°. Off is the split-screen case, which Coney
    /// does not have yet.
    bool onePlayerCamera = true;
};

/// The one FollowTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] FollowTuning& followTuning();

/// The settings a new follow camera starts with when none are given: FollowSettings' defaults (level99's values)
/// unless a debug menu changed them. A change applies when the camera is next placed (a level start or a reset).
[[nodiscard]] FollowSettings& followDefaults();

/// Yaw rate from the right stick's raw x byte (0-255, 128 at rest), radians a second: 150°/s at 0 falling to 60°/s
/// at 64, nothing between 65 and 175, -60°/s at 176 to -150°/s at 255. Positive turns the view anticlockwise (left).
[[nodiscard]] float rightStickYawRate(std::uint8_t rawX);

/// Pitch rate from the right stick's raw y byte, radians a second: 85°/s at 0 to 55°/s at 8 (stick up), -55°/s at 232
/// to -85°/s at 255, nothing between. Positive raises the camera.
[[nodiscard]] float rightStickPitchRate(std::uint8_t rawY);

/// The auto-centre rule's turn rate, radians a second, for `angle` (radians, 0 to π) between the camera's view and the
/// player's facing: nothing below 22.5°; `(a − 45°) × 2.444 + 45°` per second up to 90° (negative just above 22.5°,
/// a small turn the other way); 200°/s from 90° to 100°; then `(157.5° − a) × 2.435 + 60°`, falling to 60°/s at 157.5°.
/// Beyond 157.5° the rule runs only for a running target (`running`), and **Coney's reading** carries the same line on
/// (down to 5°/s at 180°); otherwise 0.
/// @orig 0x00129f88 Cam_Follow_AutoCentre (Cam_Follow.cpp)
[[nodiscard]] float autoCentreRate(float angle, bool running);

/// The default auto-follow rule's turn rate, radians a second, for `angle` as above: nothing below 22.5° or above
/// 157.5°; `(a − 22.5°) × 2.667` per second up to 45°; 60°/s to 135°; then falling from 120°/s to 60°/s at 157.5°.
/// @orig 0x0012a400 Cam_Follow_AutoFollowDefault (Cam_Follow.cpp)
[[nodiscard]] float defaultFollowRate(float angle);

/// The target's stored gait (`+0x1a8`), as the camera's rules read it.
inline constexpr std::uint8_t kGaitWalk = 2;
inline constexpr std::uint8_t kGaitRun = 4;
inline constexpr std::uint8_t kGaitSprint = 5;

/// The target the camera follows this update, after the target's own update.
struct FollowTarget {
    anim::Vec3 feet;      ///< The target's feet (game axes).
    float heading = 0.0F; ///< The target's facing, radians (0 facing +y).
    /// The stored gait (`+0x1a8`, 0 standing to 5 sprinting): the auto-follow rules run at the walk, run and sprint
    /// gaits, the sprint zoom at the sprint gait (docs/research/camera.md#heading).
    std::uint8_t gait = 0;
    bool airborne = false;  ///< Jumping or falling: the look-at point follows the feet without its distance limit.
    bool stickBack = false; ///< The left stick points more than 157.5° from up: no auto-follow (`+0x474`).
    /// The distance to the target's nearest enemy, from its brain (`0x0021d408`), or none when it has no enemies (byte
    /// `+0x152`): a sprint zooms in only with no enemies or the nearest within 12 m
    /// (docs/research/camera.md#sprint-zoom).
    std::optional<float> nearestEnemy;
};

/// The follow camera.
class FollowCamera {
  public:
    /// Share of the wanted move the camera covers each update.
    static constexpr float kPositionLag = 0.22F;
    /// Moves shorter than this are dropped.
    static constexpr float kMinMove = 1e-5F;
    /// Fastest pitch change toward the target without input, radians a second (85°/s).
    static constexpr float kPitchReturnRate = 1.4835F;
    /// Seconds the automatic rules are held off after any camera input.
    static constexpr float kInputHold = 0.334F;
    /// The lowest the lower pitch limit goes.
    static constexpr float kLowestPitchDegrees = -20.0F;
    /// **Coney's choice** for the pull-in, whose sphere pushes are not implemented: a main ray hit pulls the camera in
    /// to this far short of it, and never nearer the look-at point than kMinCollisionDistance.
    static constexpr float kCollisionMargin = 0.2F;
    static constexpr float kMinCollisionDistance = 0.5F;
    /// The side probes swing the camera toward the side with more room when the two differ by more than this,
    /// radians (7.5°), by kSwingShare of the turn that would even them up each update.
    static constexpr float kSwingDifference = 0.1309F;
    static constexpr float kSwingShare = 0.2F;
    /// The sprint zoom: its timer, seconds; how long after the sprint gait ends it goes back, seconds; its pitch rate
    /// when no timer runs, radians a second (30°/s).
    static constexpr float kZoomSeconds = 0.5F;
    static constexpr float kZoomBackDelay = 0.25F;
    static constexpr float kZoomPitchRate = 0.5236F;
    /// A sprint zooms in only when the nearest enemy is closer than this, metres (or there are none).
    static constexpr float kZoomEnemyRange = 12.0F;
    /// The band's ease toward its wanted near edge when no timer runs: this share of what is left a second, during a
    /// zoom and otherwise.
    static constexpr float kBandEaseZoom = 3.5F;
    static constexpr float kBandEaseFree = 4.5F;
    /// The blocked-view latch clears once the target has stood this many updates (**inferred** from the runtime: it
    /// cleared on the second update after the player stopped; the clearing branch's condition is not traced).
    static constexpr int kLatchClearUpdates = 2;
    /// Within this the band's edge and the zoom's pitch have arrived.
    static constexpr float kArrived = 1e-5F;
    /// The hard band eases back by this share of the difference per update when it shrinks (**Coney's reading** of
    /// "1% per update"); it widens at once.
    static constexpr float kHardBandEase = 0.01F;
    /// The look-at point's move per update is limited by its length `d`: all of it up to kLookAtFree, a share falling
    /// from 1 to kLookAtSlowShare between kLookAtFree and kLookAtSlow (`1 − 2 × (d − 0.4)`), kLookAtSlowShare beyond.
    static constexpr float kLookAtFree = 0.4F;
    static constexpr float kLookAtSlow = 0.8F;
    static constexpr float kLookAtSlowShare = 0.2F;
    /// The height hold's ease: this share of the way per update, times the hold's scale (`+0x398`).
    static constexpr float kHeightHoldShare = 0.3F;

    /// A camera on a target whose feet are at `targetFeet` facing `targetHeading` (radians, 0 facing +y): placed behind
    /// it at the leash band's near edge and the target pitch (**Coney's choice** for the reset, which is not traced).
    FollowCamera(anim::Vec3 targetFeet, float targetHeading, const FollowSettings& settings = followDefaults());

    /// One update of `seconds`: the band's ease; the look-at point; right stick; auto-follow; the sprint zoom; leash;
    /// pitch; height hold; position lag; hard band; world collision; timers. `rawRightX` / `rawRightY` are the pad's
    /// raw right-stick bytes; `mesh` may be null (no collision).
    /// @orig 0x0012ae58 Cam_Follow_Update (Cam_Follow.cpp)
    void update(const FollowTarget& target, std::uint8_t rawRightX, std::uint8_t rawRightY,
                const raycast::CollisionMesh* mesh, float seconds);

    /// `CamEnable(5, on)`: switches the sprint zoom on or off (`+0x468`, on when made). Turning it off cancels a zoom
    /// in progress, leaving the band and the target pitch where they are.
    /// @orig 0x00126a30 Cam_Follow_EnableSprintZoom (Cam_Follow.cpp)
    void enableSprintZoom(bool on);

    /// Enters (`on`) or leaves the height hold (`+0x453`): while it holds, the wanted position's height eases toward
    /// the height above the look-at point it had when the hold began, kHeightHoldShare × `scale` of the way an update
    /// (`+0x398`: 1, or 0.25 after `0x00125888`). What enters it in the original is not traced (inferred: the target
    /// high above the camera's ground), so Coney's player never does yet.
    void holdHeight(bool on, float scale = 1.0F);

    /// Where the camera is.
    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// Where it wants to be (`+0x250`).
    [[nodiscard]] anim::Vec3 wanted() const { return m_wanted; }
    /// The point it looks at.
    [[nodiscard]] anim::Vec3 lookAt() const { return m_lookAt; }
    /// The unit view direction, from the camera to the look-at point.
    [[nodiscard]] anim::Vec3 forward() const;
    /// The pitch the camera returns to, radians (`+0x3b4`; positive: above the look-at point, looking down).
    [[nodiscard]] float targetPitch() const { return m_targetPitch; }
    /// The lower pitch limit, radians (`+0x3b0`).
    [[nodiscard]] float lowerPitch() const { return m_lowerPitch; }
    /// The upper pitch limit, radians (`+0x3ac`), from the zoom distance: 50° at the minimum, 40° above the default,
    /// at the default 30° with one player camera and 50° otherwise.
    [[nodiscard]] float upperPitch() const;
    /// The zoom distance (`+0x400`): the minimum, default or maximum distance. **Coney's choice**: the maximum to
    /// start with, as read in the street.
    [[nodiscard]] float zoomDistance() const { return m_zoomDistance; }
    /// Seconds left of the input hold.
    [[nodiscard]] float inputHold() const { return m_inputHold; }
    /// The leash band now, metres (`+0x32c` / `+0x330`).
    [[nodiscard]] float bandNear() const { return m_bandNear; }
    [[nodiscard]] float bandFar() const { return m_bandNear + m_bandWidth; }
    /// Whether the sprint zoom is under way (`+0x448` not 0): zoomed in, or waiting or easing to go back.
    [[nodiscard]] bool sprintZoomActive() const { return m_zoomActive; }
    /// Whether the main ray was blocked in the last update (`+0x45b`).
    [[nodiscard]] bool viewBlocked() const { return m_viewBlocked; }
    /// The blocked-view latch (`+0x45d`): set by a blocked main ray, it keeps auto-follow off until the player stops.
    [[nodiscard]] bool viewLatched() const { return m_viewLatch; }
    /// The yaw the auto-follow rule turned the camera by in the last update, radians (positive anticlockwise).
    [[nodiscard]] float lastAutoTurn() const { return m_lastAutoTurn; }

  private:
    // The hard band's near edge before the first update: beyond any band, so the first update sets it at once.
    static constexpr float kNoHardBand = 1e9F;

    // Turns the wanted position about the look-at point's vertical by `angle` (radians, anticlockwise).
    // @orig 0x0012d688 Cam_Follow_Yaw (Cam_Follow.cpp)
    void yaw(float angle);
    // Moves the wanted position's pitch about the look-at point toward the target pitch, at most `maxStep` radians.
    // @orig 0x0012d4e8 Cam_Follow_Pitch (Cam_Follow.cpp)
    void pitchToward(float maxStep);
    // Steps the zoom distance (`+0x400`) to `distance`, which sets the upper pitch limit with it.
    // @orig 0x001254f0 Cam_Follow_StepZoom (Cam_Follow.cpp)
    void stepZoom(float distance);
    // Pulls the camera in when the world hides it from the look-at point, and swings it toward the side with more
    // room; notes whether the main ray was blocked.
    // @orig 0x00130990 Cam_Follow_Collide (Cam_Follow.cpp)
    void collide(const raycast::CollisionMesh& mesh, anim::Vec3 targetFeet);
    // The look-at point for `target`: the feet plus the offset, its move limited by its length unless airborne.
    // @orig 0x00127d88 Cam_Follow_LookAt (Cam_Follow.cpp)
    void followLookAt(const FollowTarget& target);
    // The auto-follow step: the auto-centre or the default rule's turn toward `target`'s facing, from `view`, the
    // camera's view at the start of the update, when the target's gait and the gates allow it.
    // @orig 0x00129c78 Cam_Follow_AutoFollow (Cam_Follow.cpp)
    void autoFollow(anim::Vec3 view, const FollowTarget& target, float seconds);
    // The sprint gait's arm and latch, which start the zoom and, 250 ms after the sprint, its way back.
    void latchSprint(bool sprinting, const FollowTarget& target, float seconds);
    // The sprint zoom's step: in toward the minimum distance and the sprint pitch while sprinting, back otherwise.
    // @orig 0x00128cf0 Cam_Follow_SprintZoom (Cam_Follow.cpp)
    void sprintZoom(bool sprinting, float seconds);
    // Moves the target pitch toward `goal`: in a straight line over the timer, or at 30°/s without one.
    void zoomPitchToward(float goal, float seconds);
    // Eases the band's near edge toward its wanted near edge (`+0x34c`), the far edge following.
    // @orig 0x0012aae0 Cam_Follow_EaseBand (Cam_Follow.cpp)
    void easeBand(float seconds);
    // The hard band's edges for the current leash band: they widen at once and shrink by kHardBandEase an update.
    void stepHardBand();

    FollowSettings m_settings;
    anim::Vec3 m_lookAt;
    anim::Vec3 m_wanted;
    anim::Vec3 m_position;
    float m_targetPitch;
    float m_lowerPitch;
    float m_zoomDistance;
    float m_bandNear;               // the leash band's near edge (+0x32c)
    float m_bandWidth;              // the far edge's distance beyond it
    float m_wantedNear = -1.0F;     // the band's wanted near edge (+0x34c); -1 for none
    float m_timer = 0.0F;           // the timed move's seconds left (+0x40c)
    double m_clock = 0.0;           // the camera's game time, seconds: the sum of its updates
    float m_inputHold = 0.0F;       // +0x368
    float m_hardNear = kNoHardBand; // the hard band's edges
    float m_hardFar = 0.0F;
    float m_sprintTime = 0.0F;         // seconds at the sprint gait (+0x36c)
    bool m_zoomOn = true;              // the sprint zoom's switch (+0x468)
    bool m_zoomArmed = false;          // armed for this sprint (+0x467)
    bool m_zoomLatched = false;        // zoomed in for this sprint (+0x466)
    bool m_zoomActive = false;         // +0x448 not 0
    double m_zoomFrom = 0.0;           // the game time after which the zoom function runs (+0x448)
    float m_savedNear = 0.0F;          // the band's near edge before the zoom (+0x3e0); 0 for none
    float m_savedZoom = 0.0F;          // the zoom distance before it (+0x3e8)
    std::optional<float> m_savedPitch; // the target pitch before it (+0x3e4)
    bool m_heightHold = false;         // +0x453
    float m_heldHeight = 0.0F;         // the wanted position's height above the look-at point the hold keeps
    float m_heightHoldScale = 1.0F;    // +0x398
    bool m_viewBlocked = false;        // +0x45b
    bool m_viewLatch = false;          // +0x45d
    int m_standingUpdates = 0;         // updates in a row the target has stood (gait 0, on the ground)
    float m_lastAutoTurn = 0.0F;
};

} // namespace coney::camera
