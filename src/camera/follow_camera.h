// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "animation/anim_math.h"
#include "raycast/collision_mesh.h"

// The follow camera behind the player in normal play (Cam_Follow): a leash camera that keeps its look-at point on the
// player, is dragged back into a distance band when the player moves away, swings round behind a moving player (the
// auto-centre rule), zooms in while the player sprints, covers a share of its wanted move each update, holds a pitch,
// turns with the right stick and pulls in when the world is in the way. Game axes (z up), stepped with the characters
// at 30 Hz; no clock, so a scripted pad gives the same views on every run (test mode).
// Research: docs/research/camera.md, docs/research/feel.md

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
    float upperPitchDegrees = 40.0F; ///< `+0x3ac` as read in the street (band 4.8-5.3 m).
    float sprintPitchDegrees = 7.0F; ///< The target pitch the sprint zoom ends at.
};

/// The follow camera's values the debug menus may edit while the game runs (Coney's tunables,
/// docs/guides/debug-menu.md#tunables), defaulting to FollowCamera's constants; read every update through
/// followTuning().
struct FollowTuning {
    float positionLag = 0.22F;         ///< FollowCamera::kPositionLag.
    float collisionMargin = 0.2F;      ///< FollowCamera::kCollisionMargin.
    float minCollisionDistance = 0.5F; ///< FollowCamera::kMinCollisionDistance.
    bool autoCentre = true;            ///< Whether the auto-centre rule runs (the per-pad option bytes, 1 in the save).
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

/// The target the camera follows this update, after the target's own update.
struct FollowTarget {
    anim::Vec3 feet;      ///< The target's feet (game axes).
    float heading = 0.0F; ///< The target's facing, radians (0 facing +y).
    /// The target moves in a way the auto-centre rule follows: walking, running, sprinting or in the air, with no start
    /// or landing clip playing (docs/research/camera.md#street).
    bool turnsCamera = false;
    bool running = false;   ///< At the run or sprint gait: the rule's "moving" argument.
    bool sprinting = false; ///< At the sprint gait: the sprint zoom.
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
    /// **Coney's choice** for the world collision, whose full probe pattern is not researched: a ray from the look-at
    /// point to the camera (mask 0x200, as the camera's rays use) pulls the camera in to this far short of a hit, and
    /// never nearer the look-at point than kMinCollisionDistance.
    static constexpr float kCollisionMargin = 0.2F;
    static constexpr float kMinCollisionDistance = 0.5F;
    static constexpr std::uint32_t kCameraRayMask = 0x200;
    /// Materials the camera's ray passes through: 30 `LOW_FENCE`, which the camera was seen to ignore through a fence
    /// climb (docs/research/camera.md#street; the test that skips it is not traced).
    static constexpr std::array<std::uint8_t, 1> kSeeThroughMaterials{30};
    /// The sprint zoom: the band's share of the way from the leash band to the minimum distance on each update of the
    /// zoom (the measured near edges 4.569 ... 3.216, then 3.0, over the 4.8-3.0 m span), and the updates the target
    /// pitch takes to reach the sprint pitch.
    static constexpr std::array<float, 15> kSprintZoomShare{0.12833F, 0.23389F, 0.32167F, 0.39722F, 0.46222F,
                                                            0.52056F, 0.57222F, 0.61889F, 0.66278F, 0.70389F,
                                                            0.74333F, 0.78278F, 0.82500F, 0.88000F, 1.0F};
    static constexpr int kSprintPitchUpdates = 14;
    /// **Coney's choice** of what ends the sprint zoom (its code is not traced): it eases back once the target has
    /// not been at the sprint gait for more than this many updates (the run stop's 8 at runtime).
    static constexpr int kSprintZoomHold = 8;
    /// The hard band eases back by this share of the difference per update when it shrinks (**Coney's reading** of
    /// "1% per update"); it widens at once.
    static constexpr float kHardBandEase = 0.01F;
    /// The look-at height's ease after a sudden rise of the target (a climb): 20% of the way per update while more
    /// than kHeightFinish is left, then the rest in kHeightFinishUpdates updates (docs/research/camera.md#street).
    /// **Coney's choice**: a rise of more than kHeightRise in one update starts it (a jump rises 0.18 m an update).
    static constexpr float kHeightEase = 0.2F;
    static constexpr float kHeightFinish = 0.6F;
    static constexpr int kHeightFinishUpdates = 2;
    static constexpr float kHeightRise = 0.5F;

    /// A camera on a target whose feet are at `targetFeet` facing `targetHeading` (radians, 0 facing +y): placed behind
    /// it at the leash band's near edge and the target pitch (**Coney's choice** for the reset, which is not traced).
    FollowCamera(anim::Vec3 targetFeet, float targetHeading, const FollowSettings& settings = followDefaults());

    /// One update of `seconds`: look-at point (eased after a climb's rise); right stick; auto-centre; sprint zoom;
    /// leash; pitch; position lag; hard band; world collision. `rawRightX` / `rawRightY` are the pad's raw right-stick
    /// bytes; `mesh` may be null (no collision).
    /// @orig 0x0012ae58 Cam_Follow_Update (Cam_Follow.cpp)
    void update(const FollowTarget& target, std::uint8_t rawRightX, std::uint8_t rawRightY,
                const raycast::CollisionMesh* mesh, float seconds);

    /// Where the camera is.
    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// Where it wants to be (`+0x250`).
    [[nodiscard]] anim::Vec3 wanted() const { return m_wanted; }
    /// The point it looks at.
    [[nodiscard]] anim::Vec3 lookAt() const { return m_lookAt; }
    /// The unit view direction, from the camera to the look-at point.
    [[nodiscard]] anim::Vec3 forward() const;
    /// The pitch the camera returns to now, radians (positive: above the look-at point, looking down): the stick's
    /// target moved toward the sprint pitch by the sprint zoom.
    [[nodiscard]] float targetPitch() const;
    /// The lower and upper pitch limits, radians.
    [[nodiscard]] float lowerPitch() const { return m_lowerPitch; }
    [[nodiscard]] float upperPitch() const { return m_upperPitch; }
    /// Seconds left of the input hold.
    [[nodiscard]] float inputHold() const { return m_inputHold; }
    /// The leash band now, metres: the settings' band moved in by the sprint zoom.
    [[nodiscard]] float bandNear() const;
    [[nodiscard]] float bandFar() const;
    /// Updates into the sprint zoom (0: none, kSprintZoomShare's size: fully in).
    [[nodiscard]] int sprintZoom() const { return m_zoom; }
    /// The yaw the auto-centre rule turned the camera by in the last update, radians (positive anticlockwise).
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
    // Pulls the camera in when the world hides it from the look-at point.
    // @orig 0x00130990 Cam_Follow_Collide (Cam_Follow.cpp)
    void collide(const raycast::CollisionMesh& mesh);
    // The look-at point for feet at `feet`: its height eased after a sudden rise, else the feet plus the offset.
    void followLookAt(anim::Vec3 feet);
    // The auto-centre rule's turn toward `heading`, from `view`, the camera's view at the start of the update.
    void autoCentre(anim::Vec3 view, float heading, bool running, float seconds);
    // The sprint zoom's step: in while the target sprints, back out once it has not for kSprintZoomHold updates.
    void stepSprintZoom(bool sprinting);
    // The hard band's edges for the current leash band: they widen at once and shrink by kHardBandEase an update.
    void stepHardBand();

    FollowSettings m_settings;
    anim::Vec3 m_lookAt;
    anim::Vec3 m_wanted;
    anim::Vec3 m_position;
    float m_targetPitch;
    float m_lowerPitch;
    float m_upperPitch;
    float m_inputHold = 0.0F;
    int m_zoom = 0;                 // updates into the sprint zoom
    int m_sinceSprint = 0;          // updates since the target was last at the sprint gait
    float m_hardNear = kNoHardBand; // the hard band's edges
    float m_hardFar = 0.0F;
    float m_lastFeetZ = 0.0F;    // the target's feet height at the last update
    bool m_heightEasing = false; // the look-at height is easing after a rise
    int m_heightFinishLeft = 0;  // updates left of the ease's finish
    float m_lastAutoTurn = 0.0F;
};

} // namespace coney::camera
