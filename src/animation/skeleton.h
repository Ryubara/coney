// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "animation/anim_pose.h"

// The game's 34-bone character skeleton: the parent table every character shares, the bones' offsets from their
// parents, and the bone transforms a pose gives. Research: docs/research/formats/animation.md#the-pose.

namespace coney::anim {

/// The parent of pose bone `bone` (0-33, checked by CONEY_ASSERT), -1 for the root: the table the original fills at
/// start-up and every character shares.
/// @orig 0x00101120 Skeleton_InitParents (unknown)
[[nodiscard]] int poseBoneParent(std::size_t bone);

/// One character's skeleton at rest: what a pose needs besides its rotations.
struct Skeleton {
    /// Each bone's position in its parent's space: the model's bone offset chunk (0x28), entry b's x, y, z.
    std::array<Vec3, kPoseBones> offsets{};
    /// a channel take the reference pose, referenceRotations()); kept for the skinning's bind-pose checks.
    /// a channel take the reference pose, referenceRotations()); the character viewer's bind view and the tests use it.
    std::array<Quat, kPoseBones> bindRotations{};
    /// Added to the z of the pelvis's animated translation (section B). animation.md places it at "entry +4" of the
    /// bone offset chunk, read literally as the float at byte 4 (entry 0's y); 0 in every model on the disc.
    float rootHeightOffset = 0.0F;
};

/// The 34 bones' transforms in the character's space (game axes, z up, the root at the origin) for `pose`. Entry 0
/// is the root's motion, not a bone: it is the identity, so bone 0's rotation (the turn per frame) never tilts the
/// body. The pelvis (bone 1) is absolute: its rotation from the pose as is, its position the pose's root translation,
/// z plus rootHeightOffset, when the pose has one, else its offset. Bones 2-33 apply their local transform (the
/// pose's rotation, the skeleton's offset) after their parent's. The root's motion (section A) is not applied: the
/// character stays at the origin.
/// Research: docs/research/formats/animation.md#bone-transforms
/// @orig 0x00104630 Instance_BuildBoneMatrices (unknown)
[[nodiscard]] std::array<Mat34, kPoseBones> boneTransforms(const Skeleton& skeleton, const Pose& pose);

} // namespace coney::anim
