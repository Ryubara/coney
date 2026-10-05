// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "animation/anim_math.h"
#include "raycast/collision_mesh.h"

// The follow camera behind the player in normal play (Cam_Follow): a leash camera that keeps its look-at point on the
// player, is dragged back into a distance band when the player moves away, covers a share of its wanted move each
// update, holds a pitch, turns with the right stick and pulls in when the world is in the way. Game axes (z up),
// stepped with the characters at 30 Hz; no clock, so a scripted pad gives the same views on every run (test mode).
// Research: docs/research/camera.md

namespace coney::camera {

/// What `CfgFollowCamera` and the tutorial's calls set, as read in level99 (docs/research/camera.md#level99-values).
struct FollowSettings {
    float minDistance = 3.0F;
    float maxDistance = 6.6F;
    float defaultDistance = 4.8F;
    float pitchDegrees = 13.0F;
    float lookAtHeight = 1.4F; ///< The look-at offset above the target's feet.
    float leashNear = 3.0F;    ///< The leash band (`+0x32c` / `+0x330`).
    float leashFar = 3.5F;
    float upperPitchDegrees = 30.0F; ///< `+0x3ac` at the default zoom with the camera option set, as at runtime.
};

/// The follow camera's values the debug menus may edit while the game runs (Coney's tunables,
/// docs/guides/debug-menu.md#tunables), defaulting to FollowCamera's constants; read every update through
/// followTuning().
struct FollowTuning {
    float positionLag = 0.22F;         ///< FollowCamera::kPositionLag.
    float collisionMargin = 0.2F;      ///< FollowCamera::kCollisionMargin.
    float minCollisionDistance = 0.5F; ///< FollowCamera::kMinCollisionDistance.
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

/// The follow camera.
class FollowCamera {
  public:
    /// Share of the wanted move the camera covers each update.
    static constexpr float kPositionLag = 0.22F;
    /// Moves shorter than this are dropped.
    static constexpr float kMinMove = 1e-5F;
    /// Fastest pitch change toward the target without input, radians a second (85°/s).
    static constexpr float kPitchReturnRate = 1.4835F;
    /// Seconds the automatic rules are held off after any camera input (no automatic rule runs yet; kept for them).
    static constexpr float kInputHold = 0.334F;
    /// The lowest the lower pitch limit goes.
    static constexpr float kLowestPitchDegrees = -20.0F;
    /// **Coney's choice** for the world collision, whose full probe pattern is not researched: a ray from the look-at
    /// point to the camera (mask 0x200, as the camera's rays use) pulls the camera in to this far short of a hit, and
    /// never nearer the look-at point than kMinCollisionDistance.
    static constexpr float kCollisionMargin = 0.2F;
    static constexpr float kMinCollisionDistance = 0.5F;
    static constexpr std::uint32_t kCameraRayMask = 0x200;

    /// A camera on a target whose feet are at `targetFeet` facing `targetHeading` (radians, 0 facing +y): placed behind
    /// it at the leash band's near edge and the target pitch (**Coney's choice** for the reset, which is not traced;
    /// at runtime the camera stood 3.0 m away at the start).
    FollowCamera(anim::Vec3 targetFeet, float targetHeading, const FollowSettings& settings = followDefaults());

    /// One update of `seconds`: look-at point; right stick; leash; pitch; position lag; hard band; world collision.
    /// `rawRightX` / `rawRightY` are the pad's raw right-stick bytes; `mesh` may be null (no collision).
    /// @orig 0x0012ae58 Cam_Follow_Update (Cam_Follow.cpp)
    void update(anim::Vec3 targetFeet, std::uint8_t rawRightX, std::uint8_t rawRightY,
                const raycast::CollisionMesh* mesh, float seconds);

    /// Where the camera is.
    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// Where it wants to be (`+0x250`).
    [[nodiscard]] anim::Vec3 wanted() const { return m_wanted; }
    /// The point it looks at.
    [[nodiscard]] anim::Vec3 lookAt() const { return m_lookAt; }
    /// The unit view direction, from the camera to the look-at point.
    [[nodiscard]] anim::Vec3 forward() const;
    /// The pitch the camera returns to, radians (positive: above the look-at point, looking down).
    [[nodiscard]] float targetPitch() const { return m_targetPitch; }
    /// The lower and upper pitch limits, radians.
    [[nodiscard]] float lowerPitch() const { return m_lowerPitch; }
    [[nodiscard]] float upperPitch() const { return m_upperPitch; }
    /// Seconds left of the input hold.
    [[nodiscard]] float inputHold() const { return m_inputHold; }

  private:
    // Turns the wanted position about the look-at point's vertical by `angle` (radians, anticlockwise).
    // @orig 0x0012d688 Cam_Follow_Yaw (Cam_Follow.cpp)
    void yaw(float angle);
    // Moves the wanted position's pitch about the look-at point toward the target pitch, at most `maxStep` radians.
    // @orig 0x0012d4e8 Cam_Follow_Pitch (Cam_Follow.cpp)
    void pitchToward(float maxStep);
    // Pulls the camera in when the world hides it from the look-at point.
    // @orig 0x00130990 Cam_Follow_Collide (Cam_Follow.cpp)
    void collide(const raycast::CollisionMesh& mesh);

    FollowSettings m_settings;
    anim::Vec3 m_lookAt;
    anim::Vec3 m_wanted;
    anim::Vec3 m_position;
    float m_targetPitch;
    float m_lowerPitch;
    float m_upperPitch;
    float m_inputHold = 0.0F;
};

} // namespace coney::camera
