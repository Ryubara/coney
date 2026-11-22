// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>

#include "animation/anim_math.h"
#include "camera/camera_view.h"

// The rail camera's keep-in-frame push: a held mode 0 rail and every mode 3 update move their target back inside the
// view's left, right and bottom edges and out of the walls between him and the camera; the camera itself does not move.
// Research: docs/research/camera.md#rail-frame

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

namespace coney::camera {

/// One plane of a view's frame: its inward unit normal and offset (a point `p` is `dot(p, normal) - w` inside it).
struct FramePlane {
    anim::Vec3 normal;
    float w = 0.0F;
};

/// The frame planes the push reads of `view`, left, right, bottom: each through the camera, along the edge of its 4:3
/// view window (viewWindow()), its normal inward.
/// @orig 0x00120c50 Camera_BuildFrustum (unknown)
[[nodiscard]] std::array<FramePlane, 3> framePlanes(const CameraView& view);

/// The target the push works on.
struct RailFrameTarget {
    anim::Vec3 feet;   ///< Where he stands now (the transform table's position).
    anim::Vec3 before; ///< Where he stood at the start of his update (human `+0x10`).
    /// His radius, metres: a player's scale x 0.5, another human's capsule radius.
    float radius = 0.5F;
    /// Jumping or in a long fall (state flags `0x1400000000`): his new height moves only 30% of the way.
    bool falling = false;
};

/// How the rail's mode asks for the push.
struct RailFrameRules {
    anim::Vec3 offset;       ///< The rail's look-at offset (`+0x320`): the push works on the feet plus it.
    float sideMargin = 0.0F; ///< How far inside the left or right edge he is kept: his radius, or setting 7 in mode 3.
    bool trailing = false;   ///< Mode 3: the line of sight starts at the camera's height, and its ray adds mask bit 0.
    /// Mode 3 on the update a target joined (`+0x3e5`): the line of sight is tested even when nothing pushed.
    bool joined = false;
};

/// The push's distances, metres (docs/research/camera.md#rail-frame).
struct RailFrameDistances {
    static constexpr float kNudge = 0.00001F;    ///< Added to each push, so the point ends just inside.
    static constexpr float kSightAhead = 2.0F;   ///< The line of sight starts this far in front of the camera ...
    static constexpr float kFarShare = 0.7F;     ///< ... and reaches at most this share of the far clip ...
    static constexpr float kRadii = 3.0F;        ///< ... plus this many radii.
    static constexpr float kMinHeadroom = 1.0F;  ///< The ground ray starts at least this far over the highest feet.
    static constexpr float kAboveGround = 0.01F; ///< The feet are put this far above the ground it hits.
    static constexpr float kFallShare = 0.3F;    ///< The share of the height change a falling target takes.
};

/// One update's push of one target: in order, the left edge (else the right) by `rules.sideMargin` and the bottom
/// edge by his radius, each moving the point (feet plus the offset) across the edge by its shortfall and the rest of
/// his move since `before` cut short by a ray through `mesh` so it stops the margin before a wall; then, when a step
/// moved him (or in mode 3 on the update he joined), a line of sight from 2 m in front of the camera that puts him his
/// radius before a wall in the way; and when anything moved him, his height from a ray down from over the highest
/// feet. Returns his new feet, or nothing when he stays. `mesh` may be null (no rays: the moves are not cut short, the
/// line of sight never hits and the height is kept). **Coney's reading**: one target, Coney's rail has one.
/// @orig 0x00140d68 CamRail_KeepTargetsInFrame (unknown)
[[nodiscard]] std::optional<anim::Vec3> keepTargetInFrame(const CameraView& view, const RailFrameTarget& target,
                                                          const RailFrameRules& rules,
                                                          const raycast::CollisionMesh* mesh);

} // namespace coney::camera
