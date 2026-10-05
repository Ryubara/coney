// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "characters/anim_set.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "raycast/collision_mesh.h"

// A human driven by a pad: the player. Its locomotion, its standing on and falling from the level's collision mesh,
// and its animation, stepped at the characters' fixed 30 Hz. Platform-neutral and deterministic: no clock, no
// randomness, so a scripted pad gives the same path on every run (test mode).
// Research: docs/research/characters.md

namespace coney::human {

/// What the human is given each update: the left stick and the camera's view direction, which turns it.
struct HumanInput {
    float stickX = 0.0F;      ///< Left stick, -1 (left) to 1 (right), after the pad's own dead zone.
    float stickY = 0.0F;      ///< Left stick, -1 (down) to 1 (up).
    anim::Vec3 cameraForward; ///< The camera's view direction; only its part across the ground is used.
};

/// The body the human moves as against walls. **Coney's choice**: the physics body's shape is not researched; a
/// sphere of this radius whose centre is this high above the feet, so kerbs and low obstacles below its bottom
/// (0.55 m) are left to the ground snap, which climbs up to 1.0 m.
inline constexpr float kBodyRadius = 0.35F;
inline constexpr float kBodyCentreHeight = 0.9F;

/// The pad-driven human: position (the feet), heading, velocity, ground and air state, and its animation.
class Human {
  public:
    /// Gravity while airborne, m/s² (1.6 g), and the fastest fall, m/s.
    static constexpr float kGravity = 15.68F;
    static constexpr float kMaxFallSpeed = 50.0F;
    /// The ground snap: a ray from this high above the feet, this long, straight down.
    static constexpr float kSnapAbove = 1.0F;
    static constexpr float kSnapLength = 1.5F;
    /// Creation's snap: a ray from 1 m above, 2.5 m long, putting the feet 0.01 above the hit.
    static constexpr float kSpawnLength = 2.5F;
    static constexpr float kSpawnGap = 0.01F;
    /// A floor a falling human lands on has a normal with z above this; a wall for the body has |z| at most this.
    static constexpr float kFloorNormalZ = 0.65F;
    /// A velocity longer than this (times the body scale, 1) is a bug: it is zeroed.
    static constexpr float kMaxSpeed = 50.0F;
    /// A human this far below the collision mesh's lowest vertex has fallen out of the world.
    static constexpr float kOutOfWorldDepth = 20.0F;
    /// After this many airborne updates with the body unable to move as long, the human is put back on its last
    /// ground.
    static constexpr std::uint32_t kStuckUpdates = 60;
    /// The landing test's segment starts this far above the feet. **Coney's choice** for the body's upper point
    /// (`+0x4e8` − 0.16 × scale, `+0x4e8` not researched).
    static constexpr float kLandingTestHeight = 1.0F;

    /// A human playing `anims` (which must outlive it) through `slots`, posed over `bindRotations` (the skeleton's).
    /// It stands at the origin facing +y until spawn().
    Human(const characters::AnimSet& anims, const AnimSlots& slots,
          std::span<const anim::Quat, anim::kPoseBones> bindRotations);

    /// Places the human at `position` (the feet, game axes) facing `headingDegrees` (0 faces +y), snapped to the
    /// ground of `mesh` (may be null: no snap) with a 2.5 m ray from 1 m above; 0.01 above the hit.
    /// @orig 0x00218008 Human_Init (unknown)
    void spawn(const raycast::CollisionMesh* mesh, anim::Vec3 position, float headingDegrees);

    /// One update of 1/30 s: the stick turned by the camera; the animation's step; the locomotion (target speed, turn,
    /// acceleration; no velocity of its own while a start clip plays); the clip's root motion added; gravity while
    /// airborne; the move against `mesh`'s walls with the ground snap, or the fall and landing; the animation state.
    /// @orig 0x00240e38 Human_PlayerLocomotion (unknown)
    /// @orig 0x0023fea8 Human_StateUpdate (unknown)
    void step(const HumanInput& input, const raycast::CollisionMesh* mesh);

    /// The pose to draw now.
    [[nodiscard]] anim::Pose pose() const { return m_animator.pose(m_bindRotations); }

    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// Radians, 0 facing +y, anticlockwise from above.
    [[nodiscard]] float heading() const { return m_heading; }
    [[nodiscard]] anim::Vec3 velocity() const { return m_velocity; }
    /// The horizontal speed (`+0x1ac`).
    [[nodiscard]] float speed() const;
    /// The gait of that speed (`+0x1a8`).
    [[nodiscard]] Gait gait() const { return gaitOfSpeed(speed(), m_animator.speeds()); }
    [[nodiscard]] bool airborne() const { return m_airborne; }
    /// The human fell more than kOutOfWorldDepth below the mesh; it stays put until spawned again.
    [[nodiscard]] bool outOfWorld() const { return m_outOfWorld; }
    /// The ground normal from the last snap.
    [[nodiscard]] anim::Vec3 groundNormal() const { return m_groundNormal; }
    /// The vertical speed at the last landing (negative), 0 before any.
    [[nodiscard]] float lastLandingSpeed() const { return m_lastLandingSpeed; }
    /// The last stick intent, after the camera's turn.
    [[nodiscard]] const StickIntent& intent() const { return m_intent; }
    [[nodiscard]] const HumanAnimator& animator() const { return m_animator; }
    [[nodiscard]] const Speeds& speeds() const { return m_animator.speeds(); }

  private:
    // The locomotion: target speed, skid, turn, acceleration; sets the horizontal velocity.
    void locomote();
    // Adds the pose's root motion (turned into world axes) to the velocity and its turn to the heading.
    // @orig 0x0023f238 Human_ApplyRootMotion (unknown)
    void applyRootMotion(const anim::Pose& pose);
    // Moves by the velocity against the walls, then snaps to the ground or starts a fall.
    // @orig 0x0023d8c8 Human_Move (unknown)
    void moveOnGround(const raycast::CollisionMesh& mesh);
    // Moves a falling human, landing on a floor it passes.
    void moveInAir(const raycast::CollisionMesh& mesh);
    // Sweeps the body from the feet at `from` by `displacement`, sliding off walls; returns where it ends, or nothing
    // when it is still blocked after three passes.
    // @orig 0x0033e278 PhysicsBody_Sweep (unknown)
    [[nodiscard]] std::optional<anim::Vec3> sweep(const raycast::CollisionMesh& mesh, anim::Vec3 from,
                                                  anim::Vec3 displacement);
    // The ground snap from the feet at `feet`: on a hit the feet go onto it; a miss starts a fall.
    // @orig 0x0023eab8 Human_SnapToGround (unknown)
    void snapToGround(const raycast::CollisionMesh& mesh, anim::Vec3 feet);
    // Lands: clears the airborne state and the vertical speed.
    // @orig 0x0023e090 Human_Land (unknown)
    void land(anim::Vec3 feet);

    HumanAnimator m_animator;
    std::array<anim::Quat, anim::kPoseBones> m_bindRotations{};
    anim::Vec3 m_position;
    float m_heading = 0.0F;
    anim::Vec3 m_velocity; // z is the vertical speed (+0x3a0)
    StickIntent m_intent;
    float m_lastMagnitude = 0.0F;
    TurnState m_turn;
    bool m_airborne = false;
    bool m_outOfWorld = false;
    std::uint32_t m_airborneUpdates = 0;
    std::uint32_t m_blockedUpdates = 0;
    anim::Vec3 m_lastGround;
    anim::Vec3 m_groundNormal{0.0F, 0.0F, 1.0F};
    float m_lastLandingSpeed = 0.0F;
    std::vector<std::uint16_t> m_nearby; // scratch for the wall test
};

/// The nearest wall a sphere at `centre` of `radius` overlaps: an enabled triangle of `mesh` with |n.z| at most
/// `maxNormalZ`, that the sphere reaches (by the distance to its closest point: face, edge or corner) from its front
/// (either side for a two-sided one). Nearness by the closest point is **Coney's choice**: the page says "nearest"
/// without saying how it measures, and a face-only test lets a body slip between two walls at a convex edge. Returns
/// the push that moves the sphere out of it, `n × (radius − plane distance)`, or nothing. `scratch` is reused between
/// calls to save allocations.
/// @orig 0x003477c0 PhysicsBody_PushOutOfWalls (unknown)
[[nodiscard]] std::optional<anim::Vec3> nearestWallPush(const raycast::CollisionMesh& mesh, anim::Vec3 centre,
                                                        float radius, float maxNormalZ,
                                                        std::vector<std::uint16_t>& scratch);

} // namespace coney::human
