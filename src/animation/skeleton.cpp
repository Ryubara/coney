// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/skeleton.h"

#include "core/assert.h"

namespace coney::anim {

namespace {

// Bone -> parent, as the original's table at 0x00597200 holds it (docs/research/formats/animation.md#the-pose).
constexpr std::array<int, kPoseBones> kPoseParents{-1, 0,  1,  1,  3,  4, 5,  6,  6,  8,  6,  10, 6,  12, 6, 6,  5,
                                                   16, 17, 18, 19, 19, 5, 22, 23, 24, 25, 25, 2,  28, 29, 2, 31, 32};

// The root (its entry is motion, not a bone) and the pelvis: the bone that takes the clip's root translation.
constexpr std::size_t kRoot = 0;
constexpr std::size_t kPelvis = 1;

} // namespace

int poseBoneParent(std::size_t bone) {
    CONEY_ASSERT(bone < kPoseBones);
    return kPoseParents[bone];
}

std::array<Mat34, kPoseBones> boneTransforms(const Skeleton& skeleton, const Pose& pose) {
    std::array<Mat34, kPoseBones> transforms{};
    // Entry 0 is the root's motion, not a bone: it stays the identity, and its turn never tilts the drawn body.
    transforms[kRoot] = transform(Quat{}, Vec3{});
    for (std::size_t bone = kPelvis; bone < kPoseBones; ++bone) {
        Vec3 offset = skeleton.offsets[bone];
        if (bone == kPelvis && pose.hasRootTranslation) {
            offset = pose.rootTranslation;
            offset.z += skeleton.rootHeightOffset;
        }
        const Mat34 local = transform(pose.rotations[bone], offset);
        // The pelvis is absolute, in the character's space. The others come after their parents in the table, so the
        // parent's transform is ready.
        transforms[bone] =
            bone == kPelvis ? local : multiply(transforms[static_cast<std::size_t>(kPoseParents[bone])], local);
    }
    return transforms;
}

} // namespace coney::anim
