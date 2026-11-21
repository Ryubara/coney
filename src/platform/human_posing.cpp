// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/human_posing.h"

#include <span>
#include <vector>

#include "characters/character_rig.h"
#include "core/worker_pool.h"
#include "platform/character_mesh.h"

namespace coney::platform {

std::vector<anim::Mat34> scaledSkinning(const anim::Skeleton& skeleton, const characters::CharacterModel& model,
                                        const anim::Pose& pose, float scale) {
    std::vector<anim::Mat34> matrices = characters::skinningMatrices(model, anim::boneTransforms(skeleton, pose));
    // A scaled body grows about the feet, in the character's space: before the placement, so the feet stay where
    // they are, and the shader's normalised normal keeps the lighting.
    if (scale != 1.0F) {
        for (anim::Mat34& matrix : matrices) {
            matrix = anim::Mat34{.x = anim::scale(matrix.x, scale),
                                 .y = anim::scale(matrix.y, scale),
                                 .z = anim::scale(matrix.z, scale),
                                 .t = anim::scale(matrix.t, scale)};
        }
    }
    return matrices;
}

// Stub for the tests' commit: every job posed whole, one after another, as the play mode posed them.
void prepareHumanPoses(WorkerPool& /*pool*/, std::span<HumanPoseJob> jobs) {
    for (HumanPoseJob& job : jobs) {
        if (job.previous != nullptr && job.current != nullptr) {
            job.pose = human::interpolate(*job.previous, *job.current, job.alpha);
        }
        job.mesh->pose(scaledSkinning(*job.skeleton, *job.model, job.pose.pose, job.scale),
                       characterPlacement(job.pose.feet, job.pose.heading, 0.0F));
    }
}

void commitHumanPose(const HumanPoseJob& /*job*/) {}

} // namespace coney::platform
