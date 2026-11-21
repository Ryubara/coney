// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_model.h"
#include "human/target_human.h"

namespace coney {
class WorkerPool;
}

namespace coney::platform {

class CharacterMesh;

/// The skinning matrices of `model` on `skeleton` in `pose`, one per skin node in the character's space, grown
/// `scale` times about the feet (the Entity inspector's body scale, Human::meshScale(); 1 leaves them as they are):
/// the arithmetic of posing one human, the same on the main thread and on a worker.
[[nodiscard]] std::vector<anim::Mat34> scaledSkinning(const anim::Skeleton& skeleton,
                                                      const characters::CharacterModel& model, const anim::Pose& pose,
                                                      float scale);

/// One human to pose: his mesh, his character's skeleton and model, his scale, and his pose, given (a scene's) or
/// blended `alpha` of the way between his last two steps' snapshots. Every pointer must stay valid, and nothing
/// may change what they point to, from prepareHumanPoses() to commitHumanPose().
struct HumanPoseJob {
    CharacterMesh* mesh = nullptr;
    const anim::Skeleton* skeleton = nullptr;
    const characters::CharacterModel* model = nullptr;
    float scale = 1.0F;
    /// With both set, the pose is blended from them (human::interpolate()) into `pose` while it is prepared;
    /// otherwise `pose` is the pose given.
    const human::TargetSnapshot* previous = nullptr;
    const human::TargetSnapshot* current = nullptr;
    float alpha = 1.0F;
    /// The pose he is drawn in: the given one, or the blend once prepared.
    human::TargetSnapshot pose{};
};

/// Works out every job's pose on `pool`'s threads, each job's arithmetic on one thread (its blend, its matrices and
/// CharacterMesh::preparePose()), touching no librw and nothing shared; returns once all are done. The results are
/// the same whatever the pool's size (docs/engine/renderer-performance.md#posing-on-workers).
void prepareHumanPoses(WorkerPool& pool, std::span<HumanPoseJob> jobs);

/// On the main thread, after prepareHumanPoses(): places the job's mesh at its pose's feet and heading
/// (CharacterMesh::commitPose()), which leaves it as CharacterMesh::pose() of the same pose would.
void commitHumanPose(const HumanPoseJob& job);

} // namespace coney::platform
