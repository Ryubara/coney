// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_model.h"
#include "characters/character_rig.h"

// Helpers of the character mesh tests (character_mesh_test.cpp, character_mesh_skin_test.cpp): where a posed mesh's
// vertices and normals stand in the world, and the placement as the play mode applied it per vertex. Include <rw.h>
// before it: librw's headers have no include guards, so this one does not include them again.
namespace coney::test {

// The placement the play mode applied to every skinned vertex before CharacterMesh::pose(): the lean's roll, the
// heading's turn, the move to the feet, and RenderWare's axes, one vertex at a time.
inline coney::anim::Vec3 placedTheOldWay(coney::anim::Vec3 v, coney::anim::Vec3 feet, float heading, float lean) {
    const float lc = std::cos(-lean);
    const float ls = std::sin(-lean);
    const coney::anim::Vec3 p{v.x * lc + v.z * ls, v.y, -v.x * ls + v.z * lc};
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    const coney::anim::Vec3 placed{p.x * c - p.y * s + feet.x, p.x * s + p.y * c + feet.y, p.z + feet.z};
    return coney::anim::Vec3{placed.x, placed.z, -placed.y};
}

// Vertex `i` of an atomic's geometry in the world: through the atomic's frame, as librw draws it.
inline rw::V3d worldVertex(rw::Atomic* atomic, std::size_t i) {
    rw::V3d out{};
    rw::V3d::transformPoints(&out, &atomic->geometry->morphTargets[0].vertices[i], 1, atomic->getFrame()->getLTM());
    return out;
}

// Normal `i` of an atomic's geometry in the world: turned by the atomic's frame.
inline rw::V3d worldNormal(rw::Atomic* atomic, std::size_t i) {
    rw::V3d out{};
    rw::V3d::transformVectors(&out, &atomic->geometry->morphTargets[0].normals[i], 1, atomic->getFrame()->getLTM());
    return out;
}

// The fixture's skinning matrices at the bind pose, with the last node turned 30 degrees and moved, so the two bones
// disagree and a vertex weighted to both bends.
inline std::vector<coney::anim::Mat34> bentMatrices(const coney::characters::CharacterModel& model) {
    const coney::anim::Skeleton skeleton = coney::characters::characterSkeleton(model);
    coney::anim::Pose pose;
    pose.rotations = skeleton.bindRotations;
    const auto matrices = coney::characters::skinningMatrices(model, coney::anim::boneTransforms(skeleton, pose));
    std::vector<coney::anim::Mat34> bent(matrices.begin(), matrices.end());
    const float c = std::cos(0.52F);
    const float s = std::sin(0.52F);
    bent.back() = coney::anim::multiply(
        coney::anim::Mat34{{c, s, 0.0F}, {-s, c, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.2F, 0.0F, 0.0F}}, bent.back());
    return bent;
}

} // namespace coney::test
