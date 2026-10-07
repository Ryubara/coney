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
/// docs/research/camera.md#street). A camera made with these and no `CfgFollowCamera` call keeps the street's band;
/// FollowCamera::configure() is the call itself, which leaves the band at the minimum.
struct FollowSettings {
    float minDistance = 3.0F;
    float maxDistance = 6.6F;
    float defaultDistance = 4.8F;
    float pitchDegrees = 13.0F;
    float fieldOfView = 65.0F; ///< Degrees (`+0x310`).
    float nearClip = 0.1F;     ///< Slot `+0x1a4`.
    float lookAtX = 0.0F;      ///< The look-at offset from the target's feet (`+0x210`), world axes.
    float lookAtY = 0.0F;
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
    /// The combat camera's test (`0x00233c50`): a pad-controlled player with a fight target, L1 held at it (record
    /// flags `0x8` and `0x4`), one player, riding nothing (docs/research/camera.md#combat-camera).
    bool lockOn = false;
    /// A lock-on button held: the hard band's far edge is doubled, at most 1.1 × the maximum distance.
    bool lockHeld = false;
    /// The point the combat camera frames: the fight target's position plus half its velocity (`+0x4e0`).
    std::optional<anim::Vec3> enemy;
    /// Where the human `CamSetSecondary` gave (`+0x320`) is, kept in view in place of auto-follow; none for none.
    std::optional<anim::Vec3> secondary;
    /// Its range (`+0x3fc`): above 0 the rule acts only within it.
    float secondaryRange = 0.0F;
};

/// The zoom presets of `CamSetFollowZoom` (docs/research/camera.md#script-calls).
enum class FollowZoom : std::uint8_t {
    Close = 0,   ///< The band's near edge at the minimum distance; the zoom step the default.
    Default = 1, ///< At the default distance; the zoom step the maximum.
    Far = 2,     ///< The band's far edge at the maximum; the zoom step the minimum.
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
    /// The height hold's scale after `CfgFollowCamera` (`0x00125888`).
    static constexpr float kConfiguredHoldScale = 0.25F;
    /// The leash band's depth: the far edge is the near edge + min(this, max − min).
    static constexpr float kBandDepth = 0.5F;
    /// The zoom step the band's near edge gives: the default at or below min + this share of (default − min), the
    /// maximum at or below default + kZoomStepFarShare of (max − default), else the minimum (`0x001254f0`).
    static constexpr float kZoomStepNearShare = 0.4F;
    static constexpr float kZoomStepFarShare = 0.6F;
    /// The field of view's ease: at most this many degrees a second.
    static constexpr float kFovRateCap = 7.5F;
    /// The combat camera: the band's wanted near edge, metres; the target pitch, degrees; the enemy held this many
    /// degrees off the view's centre, with no turn while it is between kCombatFrameLow and kCombatFrameHigh; the
    /// share of the excess turned an update, and the cap beyond kCombatFrameHigh, radians a second (640°/s).
    static constexpr float kCombatNear = 2.4F;
    static constexpr float kCombatPitchDegrees = 15.0F;
    static constexpr float kCombatFrameDegrees = 27.0F;
    static constexpr float kCombatFrameLow = 25.0F;
    static constexpr float kCombatFrameHigh = 29.0F;
    static constexpr float kCombatFrameShare = 0.455F;
    static constexpr float kCombatFrameRate = 11.17F;
    /// Keep in view (`0x0012e170`): the target may be this share of the field of view off the view's direction; the
    /// camera turns kKeepInViewShare of the excess an update, at most kKeepInViewRate radians a second (270°/s).
    static constexpr float kKeepInViewFactor = 0.25F;
    static constexpr float kKeepInViewShare = 0.35F;
    static constexpr float kKeepInViewRate = 4.712F;

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

    /// `CfgFollowCamera(min, max, default, pitch, fov, near, offset, slowmo)` on this camera (the slow-motion factor is
    /// camera::SlowMotion's): the three distances (each clamped against the others), the band at the default, the
    /// pitch as the configured and the target pitch with the view turned to it at once, the field of view easing to
    /// the new one over 1 s, the offset and the near plane, the height hold's scale 0.25; last, with one player camera,
    /// the band moved to the minimum as `CamSetFollowZoom(0)` (with two, as `CamSetFollowZoom(1)`).
    /// @orig 0x0011c0b8 CfgFollowCamera (unknown)
    /// @orig 0x00125888 Cam_Follow_ApplyConfig (Cam_Follow.cpp)
    void configure(const FollowSettings& settings);

    /// `CamSetFollowZoom(preset)`: snaps the look-at point, moves the band's near edge to the preset's distance (kept
    /// kBandDepth deep, within the distances), recomputes the lower pitch limit from the new far edge and sets the
    /// zoom step to the next preset. The camera itself is not moved: the leash drags it into the new band. With two
    /// player cameras Close acts as Default.
    /// @orig 0x0011c470 Camera_SetFollowZoom (unknown)
    void setZoom(FollowZoom preset);

    /// `CamSetFollowAngle(degrees)`: snaps the look-at point, sets the target pitch to `degrees` (positive above the
    /// player) clamped to the pitch limits, turns the view to it at once and clears the sprint latch.
    /// @orig 0x0011c3b8 Camera_SetFollowPitch (unknown)
    void setPitch(float degrees);

    /// `CameraReset` on the follow camera: behind the target at the preset distance nearest the current one (clamped
    /// to the band), with the zoom step after it, the look-at point snapped, the wanted near edge cleared, the target
    /// pitch back to the configured one (reached at once) and the field of view easing back. The band is not moved.
    /// @orig 0x00124d00 Cam_Follow_Reset (Cam_Follow.cpp)
    /// @orig 0x00124f38 Cam_Follow_PlaceBehind (Cam_Follow.cpp)
    void reset();

    /// The follow camera made current directly (also at the end of a blend): the look-at point snapped, the camera
    /// kept in its direction from it at its distance clamped to the band, the hard band set to the band and the wanted
    /// near edge cleared.
    /// @orig 0x00125cc0 Cam_Follow_Activate (Cam_Follow.cpp)
    void activate();

    /// Records where the target is without updating the camera, for a camera that is not current: reset() and
    /// activate() place it on the target's latest feet and facing.
    void observe(const FollowTarget& target);

    /// `CamEnable(0, on)`: the right stick and the zoom buttons act (on, as made) or not.
    void enableStick(bool on) { m_stickOn = on; }
    /// The player's own switch of the right stick (`0x0050b1b0[player]`), which the Warrior command menu turns off
    /// while it is up (docs/research/hud.md#warrior-command-menu); the stick turns the camera only with both on.
    void enablePadStick(bool on) { m_padStickOn = on; }

    /// Puts the camera `distance` metres from the look-at point of a target whose feet are at `targetFeet`, at the
    /// target pitch, its view facing `viewHeading` (radians, 0 facing +y), with nothing in progress: Coney's own, for
    /// a trace that starts where the original's save state had the camera (`--start`). The constructor's placement is
    /// this at the leash band's near edge behind the target.
    void place(anim::Vec3 targetFeet, float distance, float viewHeading);
    /// `CamSetFollowPos`: the camera put at `position` at once, its wanted position there too and its look-at point
    /// snapped onto the target's last feet, with no blend.
    /// @orig 0x0011c638 Camera_SetFollowPosition (unknown)
    void placeAt(anim::Vec3 position);

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
    /// The yaw the combat camera or keep-in-view turned it by in the last update, radians (positive anticlockwise).
    [[nodiscard]] float lastFrameTurn() const { return m_lastFrameTurn; }
    /// The settings it was made or configured with.
    [[nodiscard]] const FollowSettings& settings() const { return m_settings; }
    /// The field of view now, degrees, and the near plane.
    [[nodiscard]] float fieldOfView() const { return m_fov; }
    [[nodiscard]] float nearClip() const { return m_settings.nearClip; }
    /// Whether the combat camera is on (`+0x46f`).
    [[nodiscard]] bool combatOn() const { return m_combatOn; }
    /// The band's wanted near edge (`+0x34c`), or nothing while none is set.
    [[nodiscard]] std::optional<float> wantedNear() const {
        return m_wantedNear > 0.0F ? std::optional<float>(m_wantedNear) : std::nullopt;
    }

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
    // The hard band's edges for the current leash band: they widen at once and shrink by kHardBandEase an update; a
    // held lock-on button doubles the far edge, at most 1.1 × the maximum distance.
    void stepHardBand(bool lockHeld = false);
    // The look-at point of a target whose feet are at `feet`: the feet plus the offset.
    [[nodiscard]] anim::Vec3 lookAtOf(anim::Vec3 feet) const;
    // The lower pitch limit for the band's far edge, at least -20°; the target pitch is raised to it.
    // @orig 0x0012d7a8 Cam_Follow_MoveBand (Cam_Follow.cpp)
    void updateLowerPitch();
    // The zoom step the band's near edge `near` gives (the default, the maximum or the minimum distance).
    [[nodiscard]] float zoomStepFor(float near) const;
    // Turns the wanted position to the target pitch at once and puts the camera there.
    void snapPitch();
    // The field of view's ease toward the wanted one.
    void easeFieldOfView(float seconds);
    // The combat camera: on entry the band's wanted near edge to 2.4 m and the target pitch to 15°, on exit the saved
    // edge back; while on, the enemy framed 27° off centre. Returns whether it turned the camera.
    // @orig 0x0012e9a8 Cam_Follow_FrameEnemy (Cam_Follow.cpp)
    bool combat(const FollowTarget& target, float seconds);
    // Keep in view: turns toward `point` when it is more than a quarter of the field of view off the view's direction.
    // @orig 0x0012e170 Cam_Follow_KeepInView (Cam_Follow.cpp)
    void keepInView(anim::Vec3 point, float range, float seconds);

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
    float m_lastFrameTurn = 0.0F;
    float m_configuredPitch;    // +0x30c
    float m_fov;                // the field of view now
    float m_wantedFov;          // +0x394
    float m_fovRate = 0.0F;     // degrees a second (+0x39c)
    bool m_stickOn = true;      // CamEnable(0)
    bool m_padStickOn = true;   // 0x0050b1b0[player]: off while the command menu is up
    bool m_combatOn = false;    // +0x46f
    float m_combatSaved = 0.0F; // the band's near edge before the combat camera (+0x3cc); 0 for none
    anim::Vec3 m_targetFeet;    // the target's latest feet and facing
    float m_targetHeading = 0.0F;
};

} // namespace coney::camera
