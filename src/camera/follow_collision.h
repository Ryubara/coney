// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "animation/anim_math.h"
#include "raycast/collision_mesh.h"

// The follow camera's rays against the level's collision mesh: the main ray from the look-at point toward the camera,
// with its recast that ignores a disabled triangle close to the look-at point, and the side probes turned about the
// look-at point's vertical. Pure functions, so each rule can be tested on its own.
// Research: docs/research/camera.md#collision

namespace coney::camera {

/// The main ray's collision mask: type bit 9 (`0x200`) skipped, disabled triangles tested (bit 0) and those flagged
/// testable while disabled (`0x800`) too (docs/research/camera.md#collision).
inline constexpr std::uint32_t kViewRayMask = 0x200U | 0x800U | 0x1U;
/// The recast's mask: the main ray's without bit 0, so a disabled triangle is ignored.
inline constexpr std::uint32_t kViewRecastMask = 0x200U | 0x800U;
/// The recast is made when the look-at point is less than this in front of the disabled triangle's plane.
inline constexpr float kRecastNearPlane = 0.5F;
/// Materials the camera's rays pass through: 30 `LOW_FENCE`, which the camera was seen to ignore through a fence climb
/// (docs/research/camera.md#street; the test that skips it is not traced).
inline constexpr std::array<std::uint8_t, 1> kSeeThroughMaterials{30};

/// The probe angle, radians: 7° at the near edge of the distance band down to 4° at its far edge, `7° − 3° × t` with
/// `t` the camera's place in the band (clamped to it).
[[nodiscard]] float probeAngle(float distance, float bandNear, float bandFar);

/// Casts one camera ray from `from` along the unit `direction` for `length` against `mesh`, as the main ray does: with
/// kViewRayMask; when the hit is a disabled triangle and either `targetPoint` is not in front of its plane or `from`
/// is less than kRecastNearPlane in front of it, cast again with kViewRecastMask, so that triangle is ignored. Returns
/// the hit that stands, or nothing. Part of FollowCamera's collision step (`0x00130990`). Inferred: the target's
/// point (`+0x1e0`) is taken as the player's feet, and the rule as the way a panel the game switched off near the
/// player stops pulling the camera in.
[[nodiscard]] std::optional<raycast::RayHit> castViewRay(const raycast::CollisionMesh& mesh, anim::Vec3 from,
                                                         anim::Vec3 direction, float length, anim::Vec3 targetPoint);

/// The room on each side of a view: how far the ray from `from` along `direction` (length `length`) can be turned
/// about the vertical, by 1, 2 and 3 × `angle`, before a turned probe meets the world (castViewRay()). Each side's room
/// is the largest clear multiple with every smaller one clear too, at most 3 × `angle`; index 0 is anticlockwise
/// (left, seen from above), 1 clockwise.
[[nodiscard]] std::array<float, 2> sideRoom(const raycast::CollisionMesh& mesh, anim::Vec3 from, anim::Vec3 direction,
                                            float length, float angle, anim::Vec3 targetPoint);

} // namespace coney::camera
